// 行程傳遞：hook CreateProcessA/W，讓子行程（DHProtect.bin -> MSango.bin）也被注入。
// 同位元 -> 直接 Early-Bird APC 注入；跨位元（x86 平台產生 x64 遊戲）-> 委派
// 同位元的 LocaleLoader 去注入。
#include "hooks.hpp"
#include "common/config.hpp"
#include "common/inject.hpp"
#include <string>

namespace {

BOOL(WINAPI* real_CreateProcessW)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
                                  BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION) = nullptr;
BOOL(WINAPI* real_CreateProcessA)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
                                  BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION) = nullptr;

BOOL WINAPI my_CreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
                              LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                              LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    bool wanted_suspended = (flags & CREATE_SUSPENDED) != 0;
    BOOL ok = real_CreateProcessW(app, cmd, pa, ta, inh, flags | CREATE_SUSPENDED, env, dir, si, pi);
    if (ok) le::propagate_to_child(pi->hProcess, pi->hThread, wanted_suspended);
    return ok;
}

BOOL WINAPI my_CreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa,
                              LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                              LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi) {
    bool wanted_suspended = (flags & CREATE_SUSPENDED) != 0;
    BOOL ok = real_CreateProcessA(app, cmd, pa, ta, inh, flags | CREATE_SUSPENDED, env, dir, si, pi);
    if (ok) le::propagate_to_child(pi->hProcess, pi->hThread, wanted_suspended);
    return ok;
}

}  // namespace

namespace le {

void propagate_to_child(HANDLE hProc, HANDLE hThread, bool caller_wanted_suspended) {
    std::wstring dir = module_dir(g_self);
    bool child64 = process_is_64bit(hProc);
    bool self64 = sizeof(void*) == 8;

    if (child64 == self64) {
        // 同位元：直接排 Early-Bird APC
        queue_loadlibrary_apc(hProc, hThread, dir + L"\\" + hook_dll_this_arch());
    } else {
        // 跨位元：委派同位元 loader 去注入（它會排 APC，但不 resume）
        DWORD pid = GetProcessId(hProc);
        DWORD tid = GetThreadId(hThread);
        std::wstring loader = dir + (child64 ? L"\\LocaleLoader64.exe" : L"\\LocaleLoader32.exe");
        std::wstring cmd = L"\"" + loader + L"\" --inject " + std::to_wstring(pid) + L" " +
                           std::to_wstring(tid);
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION lpi{};
        std::wstring mutableCmd = cmd;
        if (real_CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                nullptr, &si, &lpi)) {
            WaitForSingleObject(lpi.hProcess, 10000);
            CloseHandle(lpi.hProcess);
            CloseHandle(lpi.hThread);
        } else {
            le::log("跨位元委派失敗: %ls\n", loader.c_str());
        }
    }

    if (!caller_wanted_suspended) ResumeThread(hThread);
}

void install_process_hooks() {
    create_hook("kernel32.dll", "CreateProcessW", (void*)my_CreateProcessW, (void**)&real_CreateProcessW);
    create_hook("kernel32.dll", "CreateProcessA", (void*)my_CreateProcessA, (void**)&real_CreateProcessA);
}

}  // namespace le
