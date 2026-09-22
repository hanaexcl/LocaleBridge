// LocaleLoader —— 以指定 profile 的語系啟動目標並注入 LocaleHook。
// 子行程傳遞由 hook DLL 內部處理，loader 啟動完即結束。位元自適應：可啟動 x86 或 x64 目標。
//
//   LocaleLoader.exe [--profile <名稱>] <目標.exe> [參數...]
//
// profile 從 HKCU\Software\LocaleBridge\Profiles\<名稱> 讀 CodePage/LCID（DWORD），
// 透過環境變數 LOCALEBRIDGE_CP / LOCALEBRIDGE_LCID 傳給目標（子行程自動繼承）。
#include <windows.h>
#include <shellapi.h>
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

std::string to_ansi(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string a(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, a.data(), n, nullptr, nullptr);
    if (!a.empty() && a.back() == '\0') a.pop_back();
    return a;
}

bool proc_is_64(HANDLE h) {
    USHORT pm = 0, nm = 0;
    if (IsWow64Process2(h, &pm, &nm)) return pm != IMAGE_FILE_MACHINE_I386;
    BOOL wow = FALSE; IsWow64Process(h, &wow); return !wow;
}

DWORD reg_dword(HKEY root, const std::wstring& sub, const wchar_t* name, DWORD fallback) {
    DWORD val = fallback, sz = sizeof(val), type = 0;
    HKEY k;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)&val, &sz);
        RegCloseKey(k);
    }
    return val;
}

void apply_profile(const std::wstring& name) {
    int cp = 936, lcid = 0x0804;
    if (!name.empty()) {
        std::wstring sub = L"Software\\LocaleBridge\\Profiles\\" + name;
        cp = (int)reg_dword(HKEY_CURRENT_USER, sub, L"CodePage", 936);
        lcid = (int)reg_dword(HKEY_CURRENT_USER, sub, L"LCID", 0x0804);
    }
    SetEnvironmentVariableW(L"LOCALEBRIDGE_CP", std::to_wstring(cp).c_str());
    SetEnvironmentVariableW(L"LOCALEBRIDGE_LCID", std::to_wstring(lcid).c_str());
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

int fail(const std::wstring& msg) {
    MessageBoxW(nullptr, msg.c_str(), L"LocaleBridge", MB_ICONERROR);
    return 1;
}

}  // namespace

// GUI(WINDOWS)子系統進入點 —— 不會有主控台黑窗。自行取命令列參數。
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    int i = 1;
    std::wstring profile;
    if (i < argc && std::wstring(argv[i]) == L"--profile") {
        if (i + 1 >= argc) return fail(L"--profile 後面需要 profile 名稱\n--profile needs a profile name");
        profile = argv[i + 1];
        i += 2;
    }
    if (i >= argc)
        return fail(L"用法 / Usage:\n  LocaleLoader.exe [--profile <名稱/name>] <目標/target.exe> [參數/args...]");

    apply_profile(profile);

    std::wstring cmd = join_args(argc, argv, i);
    std::wstring cmdMut = cmd;

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmdMut.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                        nullptr, nullptr, &si, &pi))
        return fail(L"無法啟動 / Cannot start:\n" + cmd + L"\n\nGetLastError=" + std::to_wstring(GetLastError()));

    bool child64 = proc_is_64(pi.hProcess);
    std::string dll32 = to_ansi(module_dir() + L"\\LocaleHook32.dll");
    std::string dll64 = to_ansi(module_dir() + L"\\LocaleHook64.dll");
    LPCSTR dll = child64 ? dll64.c_str() : dll32.c_str();

    if (child64 == (sizeof(void*) == 8))
        DetourUpdateProcessWithDll(pi.hProcess, &dll, 1);
    else
        DetourProcessViaHelperW(pi.dwProcessId, dll, CreateProcessW);

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
