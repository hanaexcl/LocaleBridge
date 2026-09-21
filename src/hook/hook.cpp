// LocaleHook.dll —— 用 Microsoft Detours 實作的 per-process 語系模擬。
//
// 注入（loader 與本檔的 CreateProcess hook 皆用 Detours）：改目標 import table 讓 DLL 在
// 正常初始化流程中載入，對防竄改殼(如 SecureEngine)而言是正常模組，不觸發偵測。
// 跨位元（x86 -> x64）用 DetourProcessViaHelperW，以另一位元的 rundll32 當 helper。
//
// 語系模擬：GetACP 等回報模擬值；MultiByteToWideChar/WideCharToMultiByte 把 CP_ACP 重導
// 成模擬 code page（修正走 D3D/一般轉碼的文字）；字型強制 GB2312 charset（GDI 依字型
// charset 解 DBCS，故不需改 TextOut）。刻意不 hook RegisterClass/FindWindow —— 那會造成
// 視窗類別名兩端編碼不一致，破壞「啟動器偵測遊戲視窗」的功能。
#include <windows.h>
#include <detours.h>
#include <cstdio>
#include <cstring>
#include "common/config.hpp"

#ifndef GB2312_CHARSET
#define GB2312_CHARSET 134
#endif

namespace {

char g_dll32[MAX_PATH] = "";
char g_dll64[MAX_PATH] = "";
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

// --- 原函式指標 ---
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

// --- 字型：強制 GB2312 charset ---
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
// DetourProcessViaHelperW 生 rundll32 用的 CreateProcessW 相容函式，繞過本身 hook。
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
    if (child64 == kSelf64)
        DetourUpdateProcessWithDll(hProc, &dll, 1);           // 同位元：改 import table
    else
        DetourProcessViaHelperW(pid, dll, RawCreateProcessW); // 跨位元：rundll32 helper
    (void)pid;
}

thread_local int g_depth = 0;  // 防止同一次建立被 A/W/InternalW 重複處理

BOOL WINAPI HookCreateProcessInternalW(HANDLE hTok, LPCWSTR app, LPWSTR cmd,
        LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
        LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi, PHANDLE hNewTok) {
    if (g_depth > 0)
        return OriginalCreateProcessInternalW(hTok, app, cmd, pa, ta, inh, flags, env, dir, si, pi, hNewTok);
    bool ws = (flags & CREATE_SUSPENDED) != 0;
    BOOL ok;
    { ++g_depth; ok = OriginalCreateProcessInternalW(hTok, app, cmd, pa, ta, inh, flags | CREATE_SUSPENDED, env, dir, si, pi, hNewTok); --g_depth; }
    if (ok) { inject_child(pi->hProcess, pi->dwProcessId); if (!ws) ResumeThread(pi->hThread); }
    return ok;
}
BOOL WINAPI HookCreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
                               LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                               LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    if (g_depth > 0) return OriginalCreateProcessW(app, cmd, pa, ta, inh, flags, env, dir, si, pi);
    bool ws = (flags & CREATE_SUSPENDED) != 0;
    BOOL ok;
    { ++g_depth; ok = OriginalCreateProcessW(app, cmd, pa, ta, inh, flags | CREATE_SUSPENDED, env, dir, si, pi); --g_depth; }
    if (ok) { inject_child(pi->hProcess, pi->dwProcessId); if (!ws) ResumeThread(pi->hThread); }
    return ok;
}
BOOL WINAPI HookCreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa,
                               LPSECURITY_ATTRIBUTES ta, BOOL inh, DWORD flags, LPVOID env,
                               LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi) {
    if (g_depth > 0) return OriginalCreateProcessA(app, cmd, pa, ta, inh, flags, env, dir, si, pi);
    bool ws = (flags & CREATE_SUSPENDED) != 0;
    BOOL ok;
    { ++g_depth; ok = OriginalCreateProcessA(app, cmd, pa, ta, inh, flags | CREATE_SUSPENDED, env, dir, si, pi); --g_depth; }
    if (ok) { inject_child(pi->hProcess, pi->dwProcessId); if (!ws) ResumeThread(pi->hThread); }
    return ok;
}

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
    DetourAttach(&(PVOID&)OriginalCreateProcessA, HookCreateProcessA);
    DetourAttach(&(PVOID&)OriginalCreateProcessW, HookCreateProcessW);
    if (OriginalCreateProcessInternalW)
        DetourAttach(&(PVOID&)OriginalCreateProcessInternalW, HookCreateProcessInternalW);
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
    DetourDetach(&(PVOID&)OriginalCreateProcessA, HookCreateProcessA);
    DetourDetach(&(PVOID&)OriginalCreateProcessW, HookCreateProcessW);
    if (OriginalCreateProcessInternalW)
        DetourDetach(&(PVOID&)OriginalCreateProcessInternalW, HookCreateProcessInternalW);
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
    if (DetourIsHelperProcess()) return TRUE;
    if (reason == DLL_PROCESS_ATTACH) {
        DetourRestoreAfterWith();
        init_paths(hModule);
        OriginalCreateProcessInternalW = (CreateProcessInternalW_t)GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"), "CreateProcessInternalW");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        AttachAll();
        DetourTransactionCommit();
    } else if (reason == DLL_PROCESS_DETACH) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetachAll();
        DetourTransactionCommit();
    }
    return TRUE;
}
