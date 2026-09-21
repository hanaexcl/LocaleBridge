// 文字/字型類 hook —— 修正亂碼的核心。
//   1. MultiByteToWideChar / WideCharToMultiByte：把 CP_ACP 重導成模擬 code page，
//      這修的是「程式把自己的 GBK 位元組轉成 UTF-16 再送去 D3D/控制項顯示」這條路
//      （夢三國2 遊戲內文字走這條）。
//   2. CreateFontA / CreateFontIndirectA：字型 charset 強制 GB2312，面板名用 936 轉，
//      修的是 GDI 依字型 charset 解 DBCS 字碼那條路（夢平台啟動器 UI 走這條）。
//   3. TextOutA / ExtTextOutA / DrawTextA / GetTextExtentPoint32A：直接把 ANSI 字串
//      用 936 轉成寬字元再走 W 版。
#include "hooks.hpp"
#include "common/config.hpp"
#include "common/convert.hpp"

#ifndef GB2312_CHARSET
#define GB2312_CHARSET 134
#endif

namespace {

int(WINAPI* real_MultiByteToWideChar)(UINT, DWORD, LPCCH, int, LPWSTR, int) = nullptr;
int(WINAPI* real_WideCharToMultiByte)(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL) = nullptr;
HFONT(WINAPI* real_CreateFontA)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR) = nullptr;
HFONT(WINAPI* real_CreateFontIndirectA)(const LOGFONTA*) = nullptr;
BOOL(WINAPI* real_TextOutA)(HDC, int, int, LPCSTR, int) = nullptr;
BOOL(WINAPI* real_ExtTextOutA)(HDC, int, int, UINT, const RECT*, LPCSTR, UINT, const INT*) = nullptr;
int(WINAPI* real_DrawTextA)(HDC, LPCSTR, int, LPRECT, UINT) = nullptr;
BOOL(WINAPI* real_GetTextExtentPoint32A)(HDC, LPCSTR, int, LPSIZE) = nullptr;

inline UINT remap(UINT cp) {
    return (cp == CP_ACP || cp == CP_OEMCP || cp == CP_THREAD_ACP)
               ? static_cast<UINT>(le::codepage()) : cp;
}

int WINAPI my_MultiByteToWideChar(UINT cp, DWORD f, LPCCH s, int cb, LPWSTR o, int cch) {
    return real_MultiByteToWideChar(remap(cp), f, s, cb, o, cch);
}
int WINAPI my_WideCharToMultiByte(UINT cp, DWORD f, LPCWCH s, int cc, LPSTR o, int cb,
                                  LPCCH def, LPBOOL used) {
    return real_WideCharToMultiByte(remap(cp), f, s, cc, o, cb, def, used);
}

HFONT WINAPI my_CreateFontA(int h, int w, int esc, int ori, int wt, DWORD it, DWORD un,
                            DWORD so, DWORD charset, DWORD op, DWORD cp, DWORD q, DWORD pf,
                            LPCSTR face) {
    std::wstring wface = le::widen(face, -1, le::codepage());
    return CreateFontW(h, w, esc, ori, wt, it, un, so, GB2312_CHARSET, op, cp, q, pf,
                       wface.c_str());
}

HFONT WINAPI my_CreateFontIndirectA(const LOGFONTA* a) {
    if (!a) return real_CreateFontIndirectA(a);
    LOGFONTW wlf{};
    memcpy(&wlf, a, offsetof(LOGFONTA, lfFaceName));  // 前段數值欄位佈局相同
    wlf.lfCharSet = GB2312_CHARSET;
    std::wstring wface = le::widen(a->lfFaceName, -1, le::codepage());
    lstrcpynW(wlf.lfFaceName, wface.c_str(), LF_FACESIZE);
    return CreateFontIndirectW(&wlf);
}

BOOL WINAPI my_TextOutA(HDC dc, int x, int y, LPCSTR s, int c) {
    std::wstring w = le::widen(s, c, le::codepage());
    return TextOutW(dc, x, y, w.c_str(), static_cast<int>(w.size()));
}

BOOL WINAPI my_ExtTextOutA(HDC dc, int x, int y, UINT opt, const RECT* rc, LPCSTR s, UINT c,
                           const INT* dx) {
    std::wstring w = le::widen(s, static_cast<int>(c), le::codepage());
    // ponytail: DBCS 轉寬後字數改變，逐字間距 dx 會對不上，故轉碼時忽略 dx。
    // 若某處間距歪掉再處理成重建 dx。
    return ExtTextOutW(dc, x, y, opt, rc, w.c_str(), static_cast<UINT>(w.size()), nullptr);
}

int WINAPI my_DrawTextA(HDC dc, LPCSTR s, int c, LPRECT rc, UINT fmt) {
    std::wstring w = le::widen(s, c, le::codepage());
    return DrawTextW(dc, w.c_str(), static_cast<int>(w.size()), rc, fmt);
}

BOOL WINAPI my_GetTextExtentPoint32A(HDC dc, LPCSTR s, int c, LPSIZE sz) {
    std::wstring w = le::widen(s, c, le::codepage());
    return GetTextExtentPoint32W(dc, w.c_str(), static_cast<int>(w.size()), sz);
}

}  // namespace

namespace le {

void install_text_hooks() {
    create_hook("kernel32.dll", "MultiByteToWideChar", (void*)my_MultiByteToWideChar, (void**)&real_MultiByteToWideChar);
    create_hook("kernel32.dll", "WideCharToMultiByte", (void*)my_WideCharToMultiByte, (void**)&real_WideCharToMultiByte);
    create_hook("gdi32.dll", "CreateFontA", (void*)my_CreateFontA, (void**)&real_CreateFontA);
    create_hook("gdi32.dll", "CreateFontIndirectA", (void*)my_CreateFontIndirectA, (void**)&real_CreateFontIndirectA);
    create_hook("gdi32.dll", "TextOutA", (void*)my_TextOutA, (void**)&real_TextOutA);
    create_hook("gdi32.dll", "ExtTextOutA", (void*)my_ExtTextOutA, (void**)&real_ExtTextOutA);
    create_hook("user32.dll", "DrawTextA", (void*)my_DrawTextA, (void**)&real_DrawTextA);
    create_hook("gdi32.dll", "GetTextExtentPoint32A", (void*)my_GetTextExtentPoint32A, (void**)&real_GetTextExtentPoint32A);
    // ponytail: CreateFontIndirectExA / GetGlyphOutlineA / DrawTextExA 視實測需要再補。
}

}  // namespace le
