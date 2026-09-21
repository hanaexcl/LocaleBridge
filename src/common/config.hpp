// 執行期設定：模擬的 code page / LCID，以及供子行程注入用的 DLL 名稱。
// 全部走環境變數，子行程自動繼承 -> 一次設定、整棵行程樹一致。
#pragma once
#include <windows.h>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <string>

namespace le {

inline int env_int(const char* name, int fallback) {
    char buf[32]{};
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return fallback;
    return static_cast<int>(strtol(buf, nullptr, 0));  // 支援 0x 十六進位
}

// 模擬的 ANSI code page（預設 936 GBK）
inline int codepage() {
    static int cp = env_int("LOCALEBRIDGE_CP", 936);
    return cp;
}

// 模擬的 LCID（預設 zh-CN = 0x0804）
inline LCID lcid() {
    static LCID l = static_cast<LCID>(env_int("LOCALEBRIDGE_LCID", 0x0804));
    return l;
}

// 除錯階段預設開啟；設定環境變數 LOCALEBRIDGE_LOG=0 可關閉。
inline bool logging() {
    char v[8]{};
    if (GetEnvironmentVariableA("LOCALEBRIDGE_LOG", v, sizeof(v)) && v[0] == '0') return false;
    return true;
}

inline const char* proc_base() {
    static char name[64] = "";
    if (!name[0]) {
        char path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        const char* p = strrchr(path, '\\');
        _snprintf_s(name, sizeof(name), _TRUNCATE, "%s", p ? p + 1 : path);
    }
    return name;
}

inline void log(const char* fmt, ...) {
    if (!logging()) return;
    char msg[512];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
    va_end(ap);

    char line[640];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[LocaleBridge] %s(%lu): %s",
                proc_base(), GetCurrentProcessId(), msg);
    OutputDebugStringA(line);
    // 同時寫檔，方便收集（多行程共享附加）
    FILE* f = _fsopen("C:\\ProgramData\\LocaleBridge.log", "a", _SH_DENYNO);
    if (f) { fputs(line, f); fclose(f); }
}

// 與本行程「同位元」的 hook DLL 名稱（子行程同位元時直接注入這個）
inline const wchar_t* hook_dll_this_arch() {
#ifdef _WIN64
    return L"LocaleHook64.dll";
#else
    return L"LocaleHook32.dll";
#endif
}

}  // namespace le
