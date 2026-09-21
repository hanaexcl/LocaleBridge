// 執行期設定：模擬的 code page / LCID 透過環境變數傳遞（子行程自動繼承）。
#pragma once
#include <windows.h>
#include <cstdlib>

namespace le {

inline int env_int(const char* name, int fallback) {
    char buf[32]{};
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return fallback;
    return static_cast<int>(strtol(buf, nullptr, 0));  // 支援 0x 十六進位
}

inline int codepage() {  // 模擬 ANSI code page（預設 936 GBK）
    static int cp = env_int("LOCALEBRIDGE_CP", 936);
    return cp;
}

inline LCID lcid() {  // 模擬 LCID（預設 zh-CN = 0x0804）
    static LCID l = static_cast<LCID>(env_int("LOCALEBRIDGE_LCID", 0x0804));
    return l;
}

inline const wchar_t* hook_dll_this_arch() {
#ifdef _WIN64
    return L"LocaleHook64.dll";
#else
    return L"LocaleHook32.dll";
#endif
}

}  // namespace le
