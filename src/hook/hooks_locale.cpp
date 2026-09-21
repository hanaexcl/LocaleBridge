// Locale 查詢類 hook：讓程式「以為」自己跑在 936 / zh-CN 環境。
#include "hooks.hpp"
#include "common/config.hpp"

namespace {

// --- 原函式指標 ---
UINT(WINAPI* real_GetACP)() = nullptr;
UINT(WINAPI* real_GetOEMCP)() = nullptr;
BOOL(WINAPI* real_GetCPInfo)(UINT, LPCPINFO) = nullptr;
LCID(WINAPI* real_GetThreadLocale)() = nullptr;
LCID(WINAPI* real_GetSystemDefaultLCID)() = nullptr;
LCID(WINAPI* real_GetUserDefaultLCID)() = nullptr;
LANGID(WINAPI* real_GetSystemDefaultLangID)() = nullptr;
LANGID(WINAPI* real_GetUserDefaultLangID)() = nullptr;

UINT WINAPI my_GetACP() { return static_cast<UINT>(le::codepage()); }
UINT WINAPI my_GetOEMCP() { return static_cast<UINT>(le::codepage()); }

BOOL WINAPI my_GetCPInfo(UINT cp, LPCPINFO info) {
    if (cp == CP_ACP || cp == CP_OEMCP || cp == CP_THREAD_ACP)
        cp = static_cast<UINT>(le::codepage());
    return real_GetCPInfo(cp, info);
}

LCID WINAPI my_GetThreadLocale() { return le::lcid(); }
LCID WINAPI my_GetSystemDefaultLCID() { return le::lcid(); }
LCID WINAPI my_GetUserDefaultLCID() { return le::lcid(); }
LANGID WINAPI my_GetSystemDefaultLangID() { return LANGIDFROMLCID(le::lcid()); }
LANGID WINAPI my_GetUserDefaultLangID() { return LANGIDFROMLCID(le::lcid()); }

}  // namespace

namespace le {

void install_locale_hooks() {
    create_hook("kernel32.dll", "GetACP", (void*)my_GetACP, (void**)&real_GetACP);
    create_hook("kernel32.dll", "GetOEMCP", (void*)my_GetOEMCP, (void**)&real_GetOEMCP);
    create_hook("kernel32.dll", "GetCPInfo", (void*)my_GetCPInfo, (void**)&real_GetCPInfo);
    create_hook("kernel32.dll", "GetThreadLocale", (void*)my_GetThreadLocale, (void**)&real_GetThreadLocale);
    create_hook("kernel32.dll", "GetSystemDefaultLCID", (void*)my_GetSystemDefaultLCID, (void**)&real_GetSystemDefaultLCID);
    create_hook("kernel32.dll", "GetUserDefaultLCID", (void*)my_GetUserDefaultLCID, (void**)&real_GetUserDefaultLCID);
    create_hook("kernel32.dll", "GetSystemDefaultLangID", (void*)my_GetSystemDefaultLangID, (void**)&real_GetSystemDefaultLangID);
    create_hook("kernel32.dll", "GetUserDefaultLangID", (void*)my_GetUserDefaultLangID, (void**)&real_GetUserDefaultLangID);
    // ponytail: GetLocaleInfoA/W 大多數遊戲用不到（只影響日期/數字格式），
    // 需要再補；先顧最影響顯示的 code page 與字型那條路。
}

}  // namespace le
