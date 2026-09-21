// ANSI(模擬 code page) <-> UTF-16 轉換小工具。
// 語系模擬的本質就是這一件事：把程式當成「用模擬 code page 的位元組」去解釋，
// 而不是系統的 ANSI code page。
#pragma once
#include <windows.h>
#include <string>

namespace le {

// 用指定 code page 把窄字串轉成寬字串；len<0 表示以 NUL 結尾。
inline std::wstring widen(const char* s, int len, int cp) {
    if (!s) return std::wstring();
    int n = MultiByteToWideChar(cp, 0, s, len, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, 0, s, len, w.data(), n);
    if (len < 0 && !w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

// 用指定 code page 把寬字串轉回窄字串；len<0 表示以 NUL 結尾。
inline std::string narrow(const wchar_t* s, int len, int cp) {
    if (!s) return std::string();
    int n = WideCharToMultiByte(cp, 0, s, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string a(n, '\0');
    WideCharToMultiByte(cp, 0, s, len, a.data(), n, nullptr, nullptr);
    if (len < 0 && !a.empty() && a.back() == '\0') a.pop_back();
    return a;
}

}  // namespace le
