// 行程傳遞：hook CreateProcessA / CreateProcessW / CreateProcessInternalW。
//   - 一般程式用 CreateProcessA/W 啟動子行程 -> 由 A/W hook 處理。
//   - 保護型 loader（DHProtect.bin）用底層 CreateProcessInternalW 啟動遊戲本體
//     -> 由 InternalW hook 處理。
// CreateProcessA/W 內部也會呼叫 CreateProcessInternalW，為避免同一次啟動被處理兩次，
// 用 thread-local 深度旗標：A/W hook 執行期間，InternalW hook 只單純放行。
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

// A/W hook 執行中時 > 0，讓內層的 InternalW hook 放行、不重複處理。
thread_local int g_depth = 0;

void handle_child(LPPROCESS_INFORMATION pi, LPCWSTR app, bool wanted_suspended) {
    le::log("child created pid=%lu app=%ls\n", pi->dwProcessId, app ? app : L"(cmdline)");
    le::propagate_to_child(pi->hProcess, pi->hThread, wanted_suspended);
}

BOOL WINAPI my_CreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
                              LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                              LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    bool wanted_suspended = (flags & CREATE_SUSPENDED) != 0;
    ++g_depth;
    BOOL ok = real_CreateProcessW(app, cmd, pa, ta, inh, flags | CREATE_SUSPENDED, env, dir, si, pi);
    --g_depth;
    if (ok) handle_child(pi, app, wanted_suspended);
    return ok;
}

BOOL WINAPI my_CreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa,
                              LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                              LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi) {
    bool wanted_suspended = (flags & CREATE_SUSPENDED) != 0;
    ++g_depth;
    BOOL ok = real_CreateProcessA(app, cmd, pa, ta, inh, flags | CREATE_SUSPENDED, env, dir, si, pi);
    --g_depth;
    if (ok) handle_child(pi, nullptr, wanted_suspended);
    return ok;
}

BOOL WINAPI my_CreateProcessInternalW(HANDLE hToken, LPCWSTR app, LPWSTR cmd,
                                      LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta,
                                      BOOL inh, DWORD flags, LPVOID env, LPCWSTR dir,
                                      LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi,
                                      PHANDLE hNewToken) {
    if (g_depth > 0) {  // 從 A/W hook 內部進來，放行即可（外層會處理注入）
        return real_CreateProcessInternalW(hToken, app, cmd, pa, ta, inh, flags, env, dir, si, pi, hNewToken);
    }
    bool wanted_suspended = (flags & CREATE_SUSPENDED) != 0;
    BOOL ok = real_CreateProcessInternalW(hToken, app, cmd, pa, ta, inh,
                                          flags | CREATE_SUSPENDED, env, dir, si, pi, hNewToken);
    if (ok) handle_child(pi, app, wanted_suspended);
    return ok;
}

// 用未被 hook 的底層函式啟動（避免 spawn sibling loader 時再觸發自己的 hook）。
bool spawn_raw(const std::wstring& cmdline) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring c = cmdline;
    ++g_depth;  // 讓內部 InternalW hook 放行、且不要注入這個 loader 自己
    BOOL ok = real_CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, 0,
                                  nullptr, nullptr, &si, &pi);
    --g_depth;
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
    if (!create_hook("kernel32.dll", "CreateProcessInternalW",
                     (void*)my_CreateProcessInternalW, (void**)&real_CreateProcessInternalW)) {
        le::log("hook CreateProcessInternalW 失敗（保護型 loader 啟動的遊戲可能抓不到）\n");
    }
}

}  // namespace le
