// LocaleHook.dll 進入點：注入後在獨立執行緒安裝所有 hook。
//
// 為什麼開執行緒：DLL_PROCESS_ATTACH 期間持有 loader lock，不能在裡面 LoadLibrary
// （安裝 gdi32/user32 的 hook 需要這些模組已載入）。改在 attach 時 CreateThread，
// 該執行緒在 loader lock 釋放後才跑，安全。
#include "hooks.hpp"
#include "common/config.hpp"
#include <MinHook.h>

namespace le {
HMODULE g_self = nullptr;
}

namespace le {

bool create_hook(const char* module, const char* fn, void* detour, void** original) {
    HMODULE h = GetModuleHandleA(module);
    if (!h) h = LoadLibraryA(module);  // 此處在 init 執行緒，非 loader lock，安全
    if (!h) { le::log("找不到模組 %s\n", module); return false; }
    void* target = reinterpret_cast<void*>(GetProcAddress(h, fn));
    if (!target) { le::log("找不到 %s!%s\n", module, fn); return false; }
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s != MH_OK) { le::log("MH_CreateHook %s 失敗: %d\n", fn, s); return false; }
    return true;
}

}  // namespace le

static DWORD WINAPI init_thread(LPVOID) {
    if (MH_Initialize() != MH_OK) { le::log("MH_Initialize 失敗\n"); return 1; }
    le::install_locale_hooks();
    le::install_text_hooks();
    le::install_process_hooks();
    // 視窗類函式（RegisterClass / FindWindow）刻意不 hook —— 見 README。
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) { le::log("MH_EnableHook 失敗\n"); return 1; }
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    le::log("hooks 安裝完成，cp=%d, 行程=%ls\n", le::codepage(), self);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        le::g_self = inst;
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
