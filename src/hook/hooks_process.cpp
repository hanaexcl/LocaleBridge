// 行程傳遞：hook 四個層級的行程建立 API，涵蓋整棵行程樹：
//   CreateProcessA / CreateProcessW      一般程式
//   CreateProcessInternalW               上面兩者的內部匯流點
//   NtCreateUserProcess (ntdll)          最底層 —— 保護型 loader（DHProtect.bin）
//                                        會直接呼叫這個以繞過 kernel32 層的 hook
// 這些會層層互相呼叫，為避免同一次啟動被處理多次，用 thread-local 深度旗標：
// 只有最外層（g_depth 0->1）負責注入，內層一律放行。
//
// 同位元子行程 -> Early-Bird APC 注入；跨位元（x86 loader 產生 x64 遊戲）
// -> 委派同位元的 LocaleLoader 去注入。
#include "hooks.hpp"
#include "common/config.hpp"
#include "common/inject.hpp"
#include <string>

namespace {

BOOL(WINAPI* real_CreateProcessW)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
                                  BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION) = nullptr;
BOOL(WINAPI* real_CreateProcessA)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
                                  BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION) = nullptr;

using CreateProcessInternalW_t = BOOL(WINAPI*)(
    HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
    LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION, PHANDLE);
CreateProcessInternalW_t real_CreateProcessInternalW = nullptr;

// NtCreateUserProcess（ntdll，未公開）。只需要動 ThreadFlags 與讀出 Process/Thread handle，
// 其餘結構用 PVOID 帶過。
using NtCreateUserProcess_t = LONG(NTAPI*)(PHANDLE, PHANDLE, ACCESS_MASK, ACCESS_MASK,
                                           PVOID, PVOID, ULONG, ULONG, PVOID, PVOID, PVOID);
NtCreateUserProcess_t real_NtCreateUserProcess = nullptr;

constexpr ULONG THREAD_CREATE_FLAGS_CREATE_SUSPENDED = 0x1;

// > 0 表示外層的建立 hook 正在處理中，內層一律放行、不重複注入。
thread_local int g_depth = 0;

struct Depth {  // RAII：進入時 ++，離開時 --
    Depth() { ++g_depth; }
    ~Depth() { --g_depth; }
};

void handle_child(LPPROCESS_INFORMATION pi, LPCWSTR app, bool wanted_suspended) {
    le::log("child created pid=%lu app=%ls\n", pi->dwProcessId, app ? app : L"(cmdline)");
    le::propagate_to_child(pi->hProcess, pi->hThread, wanted_suspended);
}

BOOL WINAPI my_CreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
                              LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                              LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    bool outer = (g_depth == 0);
    bool wanted_suspended = (flags & CREATE_SUSPENDED) != 0;
    BOOL ok;
    { Depth d; ok = real_CreateProcessW(app, cmd, pa, ta, inh, flags | CREATE_SUSPENDED, env, dir, si, pi); }
    if (ok && outer) handle_child(pi, app, wanted_suspended);
    return ok;
}

BOOL WINAPI my_CreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa,
                              LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                              LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi) {
    bool outer = (g_depth == 0);
    bool wanted_suspended = (flags & CREATE_SUSPENDED) != 0;
    BOOL ok;
    { Depth d; ok = real_CreateProcessA(app, cmd, pa, ta, inh, flags | CREATE_SUSPENDED, env, dir, si, pi); }
    if (ok && outer) handle_child(pi, nullptr, wanted_suspended);
    return ok;
}

BOOL WINAPI my_CreateProcessInternalW(HANDLE hToken, LPCWSTR app, LPWSTR cmd,
                                      LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta,
                                      BOOL inh, DWORD flags, LPVOID env, LPCWSTR dir,
                                      LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi,
                                      PHANDLE hNewToken) {
    bool outer = (g_depth == 0);
    bool wanted_suspended = (flags & CREATE_SUSPENDED) != 0;
    BOOL ok;
    { Depth d; ok = real_CreateProcessInternalW(hToken, app, cmd, pa, ta, inh,
                                                flags | CREATE_SUSPENDED, env, dir, si, pi, hNewToken); }
    if (ok && outer) handle_child(pi, app, wanted_suspended);
    return ok;
}

LONG NTAPI my_NtCreateUserProcess(PHANDLE ProcessHandle, PHANDLE ThreadHandle,
                                  ACCESS_MASK pAcc, ACCESS_MASK tAcc, PVOID pObj, PVOID tObj,
                                  ULONG pFlags, ULONG tFlags, PVOID pParams, PVOID createInfo,
                                  PVOID attrList) {
    bool outer = (g_depth == 0);
    bool wanted_suspended = (tFlags & THREAD_CREATE_FLAGS_CREATE_SUSPENDED) != 0;
    LONG st;
    {
        Depth d;
        st = real_NtCreateUserProcess(ProcessHandle, ThreadHandle, pAcc, tAcc, pObj, tObj, pFlags,
                                      tFlags | THREAD_CREATE_FLAGS_CREATE_SUSPENDED, pParams,
                                      createInfo, attrList);
    }
    if (st >= 0 && outer && ProcessHandle && ThreadHandle && *ProcessHandle && *ThreadHandle) {
        le::log("NtCreateUserProcess child pid=%lu\n", GetProcessId(*ProcessHandle));
        le::propagate_to_child(*ProcessHandle, *ThreadHandle, wanted_suspended);
    }
    return st;
}

// 用未被 hook 的路徑啟動 sibling loader（Depth 讓內層放行，也不會注入 loader 自己）。
bool spawn_raw(const std::wstring& cmdline) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring c = cmdline;
    BOOL ok;
    { Depth d; ok = real_CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, 0,
                                        nullptr, nullptr, &si, &pi); }
    if (ok) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok != FALSE;
}

}  // namespace

namespace le {

void propagate_to_child(HANDLE hProc, HANDLE hThread, bool caller_wanted_suspended) {
    std::wstring dir = module_dir(g_self);
    bool child64 = process_is_64bit(hProc);
    bool self64 = sizeof(void*) == 8;

    if (child64 == self64) {
        bool ok = queue_loadlibrary_apc(hProc, hThread, dir + L"\\" + hook_dll_this_arch());
        le::log("  同位元注入 %s\n", ok ? "OK" : "失敗");
    } else {
        DWORD pid = GetProcessId(hProc);
        DWORD tid = GetThreadId(hThread);
        std::wstring loader = dir + (child64 ? L"\\LocaleLoader64.exe" : L"\\LocaleLoader32.exe");
        std::wstring cmd = L"\"" + loader + L"\" --inject " + std::to_wstring(pid) + L" " +
                           std::to_wstring(tid);
        bool ok = spawn_raw(cmd);
        le::log("  跨位元委派 %ls -> %s\n", loader.c_str(), ok ? "OK" : "失敗");
    }

    if (!caller_wanted_suspended) ResumeThread(hThread);
}

void install_process_hooks() {
    create_hook("kernel32.dll", "CreateProcessW", (void*)my_CreateProcessW, (void**)&real_CreateProcessW);
    create_hook("kernel32.dll", "CreateProcessA", (void*)my_CreateProcessA, (void**)&real_CreateProcessA);
    create_hook("kernel32.dll", "CreateProcessInternalW",
                (void*)my_CreateProcessInternalW, (void**)&real_CreateProcessInternalW);
    if (!create_hook("ntdll.dll", "NtCreateUserProcess",
                     (void*)my_NtCreateUserProcess, (void**)&real_NtCreateUserProcess)) {
        le::log("hook NtCreateUserProcess 失敗（保護型 loader 啟動的遊戲可能抓不到）\n");
    }
}

}  // namespace le
