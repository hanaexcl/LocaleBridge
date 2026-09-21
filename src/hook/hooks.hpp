// 各類 hook 的安裝介面，以及 MinHook 的建立小工具。
#pragma once
#include <windows.h>

namespace le {

// 在指定模組找到函式並建立 hook（尚未 enable；統一在 dllmain 最後一次 enable）。
bool create_hook(const char* module, const char* fn, void* detour, void** original);

void install_locale_hooks();   // GetACP / GetLocaleInfo / ...
void install_text_hooks();     // MultiByteToWideChar / CreateFontA / TextOutA / ...
void install_process_hooks();  // CreateProcessA/W 傳遞注入

// 供 process hook 使用：把本 hook DLL 注入某子行程（處理同/跨位元）。
void propagate_to_child(HANDLE hProc, HANDLE hThread, bool caller_wanted_suspended);

// dllmain 設定的自身模組 handle（用來定位同資料夾的 DLL / loader）。
extern HMODULE g_self;

}  // namespace le
