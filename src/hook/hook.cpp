// LocaleHook.dll —— 用 Microsoft Detours 實作的 per-process 語系模擬（架構對齊
// InWILL/Locale_Remulator）。
//
// 注入方式（由 loader 與本檔的 CreateProcess hook 使用）都走 Detours：
//   * DetourCreateProcessWithDllExW / DetourUpdateProcessWithDll 改目標 import table，
//     讓 DLL 在「正常初始化流程」中載入 —— 對 SecureEngine 而言就是正常匯入的模組，
//     不是外來注入，故不觸發防竄改（這是手動 APC/CreateRemoteThread 會被擋的原因）。
//   * 跨位元（x86 保護 loader 產生 x64 遊戲）用 DetourProcessViaHelperW，Detours 會以
//     另一位元的 rundll32 當 helper 完成注入。
//
// hook 集合與 LR 一致：locale 查詢、MultiByteToWideChar/WideCharToMultiByte 把 CP_ACP
// 重導 936、字型強制 GB2312 charset（GDI 依字型 charset 解 DBCS，故不需改 TextOut）、
// 以及依位元不同的 CreateProcess 傳遞。刻意不 hook RegisterClass/FindWindow（避免像 LR
// 早期版本破壞平台的視窗偵測）。
#include <windows.h>
#include <detours.h>
#include "common/config.hpp"
#include "common/convert.hpp"

#ifndef GB2312_CHARSET
#define GB2312_CHARSET 134
#endif

namespace {

char g_dll32[MAX_PATH] = "";  // LocaleHook32.dll 完整路徑
char g_dll64[MAX_PATH] = "";  // LocaleHook64.dll 完整路徑
constexpr bool kSelf64 = sizeof(void*) == 8;

UINT remap(UINT cp) {
    return (cp == CP_ACP || cp == CP_OEMCP || cp == CP_THREAD_ACP)
               ? static_cast<UINT>(le::codepage()) : cp;
}

bool proc_is_64(HANDLE h) {
    USHORT pm = 0, nm = 0;
    if (IsWow64Process2(h, &pm, &nm)) return pm != IMAGE_FILE_MACHINE_I386;
    BOOL wow = FALSE; IsWow64Process(h, &wow); return !wow;
}

// --- 原函式指標（documented：靜態初始化為真實 API）---
auto OriginalGetACP = GetACP;
auto OriginalGetOEMCP = GetOEMCP;
auto OriginalGetCPInfo = GetCPInfo;
auto OriginalGetThreadLocale = GetThreadLocale;
auto OriginalGetSystemDefaultLCID = GetSystemDefaultLCID;
auto OriginalGetUserDefaultLCID = GetUserDefaultLCID;
auto OriginalGetSystemDefaultLangID = GetSystemDefaultLangID;
auto OriginalGetUserDefaultLangID = GetUserDefaultLangID;
auto OriginalMultiByteToWideChar = MultiByteToWideChar;
auto OriginalWideCharToMultiByte = WideCharToMultiByte;
auto OriginalCreateFontA = CreateFontA;
auto OriginalCreateFontIndirectA = CreateFontIndirectA;
auto OriginalCreateFontIndirectExA = CreateFontIndirectExA;
auto OriginalCreateProcessA = CreateProcessA;
auto OriginalCreateProcessW = CreateProcessW;

using CreateProcessInternalW_t = BOOL(WINAPI*)(HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION, PHANDLE);
CreateProcessInternalW_t OriginalCreateProcessInternalW = nullptr;

// --- locale ---
UINT WINAPI HookGetACP() { return (UINT)le::codepage(); }
UINT WINAPI HookGetOEMCP() { return (UINT)le::codepage(); }
BOOL WINAPI HookGetCPInfo(UINT cp, LPCPINFO i) { return OriginalGetCPInfo(remap(cp), i); }
LCID WINAPI HookGetThreadLocale() { return le::lcid(); }
LCID WINAPI HookGetSystemDefaultLCID() { return le::lcid(); }
LCID WINAPI HookGetUserDefaultLCID() { return le::lcid(); }
LANGID WINAPI HookGetSystemDefaultLangID() { return LANGIDFROMLCID(le::lcid()); }
LANGID WINAPI HookGetUserDefaultLangID() { return LANGIDFROMLCID(le::lcid()); }

// --- code page ---
int WINAPI HookMultiByteToWideChar(UINT cp, DWORD f, LPCCH s, int cb, LPWSTR o, int cch) {
    return OriginalMultiByteToWideChar(remap(cp), f, s, cb, o, cch);
}
int WINAPI HookWideCharToMultiByte(UINT cp, DWORD f, LPCWCH s, int cc, LPSTR o, int cb, LPCCH d, LPBOOL u) {
    return OriginalWideCharToMultiByte(remap(cp), f, s, cc, o, cb, d, u);
}

// --- 字型：強制 GB2312 charset（GDI 依字型 charset 解 DBCS -> 簡體正確）---
HFONT WINAPI HookCreateFontA(int h, int w, int e, int o, int wt, DWORD it, DWORD un, DWORD so,
                             DWORD, DWORD op, DWORD cp, DWORD q, DWORD pf, LPCSTR face) {
    return OriginalCreateFontA(h, w, e, o, wt, it, un, so, GB2312_CHARSET, op, cp, q, pf, face);
}
HFONT WINAPI HookCreateFontIndirectA(const LOGFONTA* lf) {
    if (!lf) return OriginalCreateFontIndirectA(lf);
    LOGFONTA c = *lf; c.lfCharSet = GB2312_CHARSET;
    return OriginalCreateFontIndirectA(&c);
}
HFONT WINAPI HookCreateFontIndirectExA(const ENUMLOGFONTEXDVA* lf) {
    if (!lf) return OriginalCreateFontIndirectExA(lf);
    ENUMLOGFONTEXDVA c = *lf; c.elfEnumLogfontEx.elfLogFont.lfCharSet = GB2312_CHARSET;
    return OriginalCreateFontIndirectExA(&c);
}

// --- 行程傳遞 ---
// 供 DetourProcessViaHelperW 生 rundll32 用的 CreateProcessW 相容函式，繞過本身的 hook。
BOOL WINAPI RawCreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
                              LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                              LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    if (OriginalCreateProcessInternalW)
        return OriginalCreateProcessInternalW(nullptr, app, cmd, pa, ta, inh, flags, env, dir, si, pi, nullptr);
    return OriginalCreateProcessW(app, cmd, pa, ta, inh, flags, env, dir, si, pi);
}

void inject_child(HANDLE hProc, DWORD pid) {
    bool child64 = proc_is_64(hProc);
    LPCSTR dll = child64 ? g_dll64 : g_dll32;
    if (child64 == kSelf64) {
        BOOL ok = DetourUpdateProcessWithDll(hProc, &dll, 1);
        le::log("inject child pid=%lu 同位元 %s\n", pid, ok ? "OK" : "失敗");
    } else {
        BOOL ok = DetourProcessViaHelperW(pid, dll, RawCreateProcessW);
        le::log("inject child pid=%lu 跨位元(helper) %s\n", pid, ok ? "OK" : "失敗");
    }
}

#ifdef _WIN64  // 64 位元行程：hook CreateProcessA/W（用 DetourCreateProcessWithDllEx）
BOOL WINAPI HookCreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
                               LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                               LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    le::log("HookCreateProcessW\n");
    return DetourCreateProcessWithDllExW(app, cmd, pa, ta, inh, flags, env, dir, si, pi,
                                         g_dll64, OriginalCreateProcessW);
}
BOOL WINAPI HookCreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa,
                               LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                               LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi) {
    le::log("HookCreateProcessA\n");
    return DetourCreateProcessWithDllExA(app, cmd, pa, ta, inh, flags, env, dir, si, pi,
                                         g_dll64, OriginalCreateProcessA);
}
#else  // 32 位元行程：hook CreateProcessInternalW（保護型 loader 走這條）
BOOL WINAPI HookCreateProcessInternalW(HANDLE hTok, LPCWSTR app, LPWSTR cmd,
        LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
        LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi, PHANDLE hNewTok) {
    if (!OriginalCreateProcessInternalW(hTok, app, cmd, pa, ta, inh, flags | CREATE_SUSPENDED,
                                        env, dir, si, pi, hNewTok))
        return FALSE;
    le::log("HookCreateProcessInternalW child pid=%lu app=%ls\n", pi->dwProcessId, app ? app : L"(cmd)");
    inject_child(pi->hProcess, pi->dwProcessId);
    if (!(flags & CREATE_SUSPENDED)) ResumeThread(pi->hThread);
    return TRUE;
}
#endif

void AttachAll() {
    DetourAttach(&(PVOID&)OriginalGetACP, HookGetACP);
    DetourAttach(&(PVOID&)OriginalGetOEMCP, HookGetOEMCP);
    DetourAttach(&(PVOID&)OriginalGetCPInfo, HookGetCPInfo);
    DetourAttach(&(PVOID&)OriginalGetThreadLocale, HookGetThreadLocale);
    DetourAttach(&(PVOID&)OriginalGetSystemDefaultLCID, HookGetSystemDefaultLCID);
    DetourAttach(&(PVOID&)OriginalGetUserDefaultLCID, HookGetUserDefaultLCID);
    DetourAttach(&(PVOID&)OriginalGetSystemDefaultLangID, HookGetSystemDefaultLangID);
    DetourAttach(&(PVOID&)OriginalGetUserDefaultLangID, HookGetUserDefaultLangID);
    DetourAttach(&(PVOID&)OriginalMultiByteToWideChar, HookMultiByteToWideChar);
    DetourAttach(&(PVOID&)OriginalWideCharToMultiByte, HookWideCharToMultiByte);
    DetourAttach(&(PVOID&)OriginalCreateFontA, HookCreateFontA);
    DetourAttach(&(PVOID&)OriginalCreateFontIndirectA, HookCreateFontIndirectA);
    DetourAttach(&(PVOID&)OriginalCreateFontIndirectExA, HookCreateFontIndirectExA);
#ifdef _WIN64
    DetourAttach(&(PVOID&)OriginalCreateProcessA, HookCreateProcessA);
    DetourAttach(&(PVOID&)OriginalCreateProcessW, HookCreateProcessW);
#else
    if (OriginalCreateProcessInternalW)
        DetourAttach(&(PVOID&)OriginalCreateProcessInternalW, HookCreateProcessInternalW);
#endif
}

void DetachAll() {
    DetourDetach(&(PVOID&)OriginalGetACP, HookGetACP);
    DetourDetach(&(PVOID&)OriginalGetOEMCP, HookGetOEMCP);
    DetourDetach(&(PVOID&)OriginalGetCPInfo, HookGetCPInfo);
    DetourDetach(&(PVOID&)OriginalGetThreadLocale, HookGetThreadLocale);
    DetourDetach(&(PVOID&)OriginalGetSystemDefaultLCID, HookGetSystemDefaultLCID);
    DetourDetach(&(PVOID&)OriginalGetUserDefaultLCID, HookGetUserDefaultLCID);
    DetourDetach(&(PVOID&)OriginalGetSystemDefaultLangID, HookGetSystemDefaultLangID);
    DetourDetach(&(PVOID&)OriginalGetUserDefaultLangID, HookGetUserDefaultLangID);
    DetourDetach(&(PVOID&)OriginalMultiByteToWideChar, HookMultiByteToWideChar);
    DetourDetach(&(PVOID&)OriginalWideCharToMultiByte, HookWideCharToMultiByte);
    DetourDetach(&(PVOID&)OriginalCreateFontA, HookCreateFontA);
    DetourDetach(&(PVOID&)OriginalCreateFontIndirectA, HookCreateFontIndirectA);
    DetourDetach(&(PVOID&)OriginalCreateFontIndirectExA, HookCreateFontIndirectExA);
#ifdef _WIN64
    DetourDetach(&(PVOID&)OriginalCreateProcessA, HookCreateProcessA);
    DetourDetach(&(PVOID&)OriginalCreateProcessW, HookCreateProcessW);
#else
    if (OriginalCreateProcessInternalW)
        DetourDetach(&(PVOID&)OriginalCreateProcessInternalW, HookCreateProcessInternalW);
#endif
}

void init_paths(HMODULE self) {
    char dir[MAX_PATH]{};
    GetModuleFileNameA(self, dir, MAX_PATH);
    char* p = strrchr(dir, '\\');
    if (p) *p = '\0';
    _snprintf_s(g_dll32, sizeof(g_dll32), _TRUNCATE, "%s\\LocaleHook32.dll", dir);
    _snprintf_s(g_dll64, sizeof(g_dll64), _TRUNCATE, "%s\\LocaleHook64.dll", dir);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (DetourIsHelperProcess()) return TRUE;  // 作為 rundll32 helper 被載入時，交給 Detours

    if (reason == DLL_PROCESS_ATTACH) {
        DetourRestoreAfterWith();
        init_paths(hModule);
        OriginalCreateProcessInternalW = (CreateProcessInternalW_t)GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"), "CreateProcessInternalW");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        AttachAll();
        LONG err = DetourTransactionCommit();
        wchar_t self[MAX_PATH]{}; GetModuleFileNameW(hModule, self, MAX_PATH);
        le::log("hooks attached (ret=%ld) cp=%d 行程=%ls\n", err, le::codepage(), self);
    } else if (reason == DLL_PROCESS_DETACH) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetachAll();
        DetourTransactionCommit();
    }
    return TRUE;
}
