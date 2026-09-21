// LocaleLoader —— 用 Detours 啟動目標並把 LocaleHook 注入其 import table。
// 子行程（DHProtect -> MSango 等）的傳遞注入由 hook DLL 內部處理，loader 啟動完即結束。
//
//   LocaleLoader32.exe  <target.exe> [args...]
#include <windows.h>
#include <detours.h>
#include <string>
#include "common/config.hpp"

namespace {

std::wstring module_dir() {
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    size_t k = s.find_last_of(L'\\');
    return k == std::wstring::npos ? std::wstring() : s.substr(0, k);
}

std::string dll_path_ansi() {
    // 與 loader 同位元的 hook DLL（初始目標與 loader 同位元；子行程由 hook 依位元自行選擇）
    std::wstring w = module_dir() + L"\\" + le::hook_dll_this_arch();
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string a(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, a.data(), n, nullptr, nullptr);
    if (!a.empty() && a.back() == '\0') a.pop_back();
    return a;
}

std::wstring join_args(int argc, wchar_t** argv, int start) {
    std::wstring out;
    for (int i = start; i < argc; ++i) {
        std::wstring a = argv[i];
        bool q = a.find(L' ') != std::wstring::npos && a.front() != L'"';
        if (!out.empty()) out += L' ';
        out += q ? (L'"' + a + L'"') : a;
    }
    return out;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        MessageBoxW(nullptr,
            L"用法:\n  LocaleLoader.exe <目標程式.exe> [參數...]\n\n"
            L"例:\n  LocaleLoader32.exe \"D:\\dianhun\\mpl\\夢平台.exe\"",
            L"LocaleBridge", MB_ICONINFORMATION);
        return 1;
    }

    std::wstring cmd = join_args(argc, argv, 1);
    std::string dll = dll_path_ansi();
    LPCSTR dllp = dll.c_str();

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmd;

    BOOL ok = DetourCreateProcessWithDllExW(
        nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_DEFAULT_ERROR_MODE,
        nullptr, nullptr, &si, &pi, dllp, CreateProcessW);

    if (!ok) {
        DWORD e = GetLastError();
        le::log("loader DetourCreateProcessWithDllExW 失敗 err=%lu cmd=%ls\n", e, cmd.c_str());
        MessageBoxW(nullptr, (L"無法啟動:\n" + cmd).c_str(), L"LocaleBridge", MB_ICONERROR);
        return 1;
    }
    le::log("loader 已啟動並注入 pid=%lu dll=%hs\n", pi.dwProcessId, dllp);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
