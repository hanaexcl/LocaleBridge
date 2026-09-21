// LocaleLoader —— 啟動目標並注入 LocaleHook，或受委派去注入某個既有行程。
//
//   LocaleLoader32.exe  <target.exe> [args...]   一般用法
//   LocaleLoaderNN.exe  --inject <pid> <tid>     由 hook 的跨位元傳遞呼叫（只排 APC，不 resume）
#include <windows.h>
#include <string>
#include <vector>
#include "common/config.hpp"
#include "common/inject.hpp"

namespace {

std::wstring hook_dll_path() {
    return le::module_dir(nullptr) + L"\\" + le::hook_dll_this_arch();
}

int usage() {
    MessageBoxW(nullptr,
                L"用法:\n  LocaleLoader.exe <目標程式.exe> [參數...]\n\n"
                L"例:\n  LocaleLoader32.exe \"D:\\dianhun\\mpl\\夢平台.exe\"",
                L"LocaleBridge", MB_ICONINFORMATION);
    return 1;
}

// 受委派模式：對既有（通常 suspended 的）行程排入 LoadLibrary APC，不 resume。
int do_inject(DWORD pid, DWORD tid) {
    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
        PROCESS_QUERY_INFORMATION, FALSE, pid);
    HANDLE hThread = OpenThread(THREAD_SET_CONTEXT, FALSE, tid);
    if (!hProc || !hThread) {
        le::log("--inject OpenProcess/Thread 失敗 pid=%lu tid=%lu err=%lu\n",
                pid, tid, GetLastError());
        return 1;
    }
    bool ok = le::queue_loadlibrary_apc(hProc, hThread, hook_dll_path());
    le::log("--inject pid=%lu tid=%lu 排 APC %s (dll=%ls)\n",
            pid, tid, ok ? "OK" : "失敗", hook_dll_path().c_str());
    CloseHandle(hThread);
    CloseHandle(hProc);
    return ok ? 0 : 1;
}

// 一般模式：CreateProcess(SUSPENDED) -> 注入 -> resume。
int do_launch(const std::wstring& cmdline) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmdline;  // CreateProcessW 需要可寫緩衝
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        MessageBoxW(nullptr, (L"無法啟動:\n" + cmdline).c_str(), L"LocaleBridge", MB_ICONERROR);
        return 1;
    }

    bool child64 = le::process_is_64bit(pi.hProcess);
    bool self64 = sizeof(void*) == 8;
    le::log("launch pid=%lu child64=%d self64=%d cmd=%ls\n",
            pi.dwProcessId, child64, self64, cmdline.c_str());
    bool ok;
    if (child64 == self64) {
        ok = le::queue_loadlibrary_apc(pi.hProcess, pi.hThread, hook_dll_path());
    } else {
        // 目標與本 loader 位元不同，委派另一版 loader 去排 APC
        std::wstring sibling = le::module_dir(nullptr) +
            (child64 ? L"\\LocaleLoader64.exe" : L"\\LocaleLoader32.exe");
        std::wstring cmd = L"\"" + sibling + L"\" --inject " +
            std::to_wstring(pi.dwProcessId) + L" " + std::to_wstring(GetThreadId(pi.hThread));
        STARTUPINFOW ssi{sizeof(ssi)};
        PROCESS_INFORMATION spi{};
        std::wstring sc = cmd;
        ok = CreateProcessW(nullptr, sc.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                            &ssi, &spi) != FALSE;
        if (ok) {
            WaitForSingleObject(spi.hProcess, 10000);
            CloseHandle(spi.hProcess);
            CloseHandle(spi.hThread);
        }
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return ok ? 0 : 1;
}

// 把 argv[start..] 組成一條命令列，各段視需要加引號。
std::wstring join_args(int argc, wchar_t** argv, int start) {
    std::wstring out;
    for (int i = start; i < argc; ++i) {
        std::wstring a = argv[i];
        bool need_quote = a.find(L' ') != std::wstring::npos && a.front() != L'"';
        if (!out.empty()) out += L' ';
        if (need_quote) out += L'"' + a + L'"';
        else out += a;
    }
    return out;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return usage();

    if (std::wstring(argv[1]) == L"--inject") {
        if (argc < 4) return usage();
        return do_inject(_wtoi(argv[2]), _wtoi(argv[3]));
    }

    return do_launch(join_args(argc, argv, 1));
}
