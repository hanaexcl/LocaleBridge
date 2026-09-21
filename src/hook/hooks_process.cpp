// 行程傳遞：hook CreateProcessInternalW —— 這是 kernel32 裡所有 CreateProcess*
// （A / W / AsUser）最後匯流的底層函式，連保護型 loader（DHProtect.bin）用來啟動
// 遊戲本體那種非標準路徑也會經過它。只 hook 這一個點，就能涵蓋整棵行程樹，
// 且不會像同時 hook A/W 那樣重複注入。
//
// 同位元子行程 -> Early-Bird APC 直接注入；跨位元（x86 保護 loader 產生 x64 遊戲）
// -> 委派同位元的 LocaleLoader 去注入。
#include "hooks.hpp"
#include "common/config.hpp"
#include "common/inject.hpp"
#include <string>

namespace {

using CreateProcessInternalW_t = BOOL(WINAPI*)(
    HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
    LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION, PHANDLE);

CreateProcessInternalW_t real_CreateProcessInternalW = nullptr;

BOOL WINAPI my_CreateProcessInternalW(HANDLE hToken, LPCWSTR app, LPWSTR cmd,
                                      LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta,
                                      BOOL inh, DWORD flags, LPVOID env, LPCWSTR dir,
                                      LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi,
                                      PHANDLE hNewToken) {
    bool wanted_suspended = (flags & CREATE_SUSPENDED) != 0;
    BOOL ok = real_CreateProcessInternalW(hToken, app, cmd, pa, ta, inh,
                                          flags | CREATE_SUSPENDED, env, dir, si, pi, hNewToken);
    if (ok) {
        le::log("child created pid=%lu app=%ls\n", pi->dwProcessId, app ? app : L"(cmdline)");
        le::propagate_to_child(pi->hProcess, pi->hThread, wanted_suspended);
    }
    return ok;
}

// 用未被 hook 的底層函式啟動（避免 spawn sibling loader 時再觸發自己的 hook）。
bool spawn_raw(const std::wstring& cmdline) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring c = cmdline;
    BOOL ok = real_CreateProcessInternalW(nullptr, nullptr, c.data(), nullptr, nullptr, FALSE, 0,
                                          nullptr, nullptr, &si, &pi, nullptr);
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
    if (!create_hook("kernel32.dll", "CreateProcessInternalW",
                     (void*)my_CreateProcessInternalW, (void**)&real_CreateProcessInternalW)) {
        le::log("hook CreateProcessInternalW 失敗（子行程不會被傳遞注入）\n");
    }
}

}  // namespace le
