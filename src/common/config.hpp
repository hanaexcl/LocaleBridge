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

inline bool logging() {
    static bool on = GetEnvironmentVariableA("LOCALEBRIDGE_LOG", nullptr, 0) > 0;
    return on;
}

inline void log(const char* fmt, ...) {
    if (!logging()) return;
    char buf[512];
    int k = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[LocaleBridge] ");
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf + k, sizeof(buf) - k, _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
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
