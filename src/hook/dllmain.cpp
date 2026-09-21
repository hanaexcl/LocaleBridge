// LocaleHook.dll 進入點。
//
// 時機是關鍵：本 DLL 以 Early-Bird APC 注入 —— DllMain 在目標「主執行緒進入點之前」
// 就執行。所以：
//   * 行程建立 hook（kernel32 CreateProcess* / ntdll NtCreateUserProcess）在 DllMain
//     裡「同步」裝好。這些模組本來就已載入、不需 LoadLibrary，可安全在 loader lock 下裝；
//     裝好後主執行緒才開始跑，就能攔到它接下來建立子行程（例如保護型 loader 立刻生出的
//     遊戲本體）—— 這正是我們之前輸掉的競速。
//   * 文字/字型 hook（gdi32/user32）需要 LoadLibrary 那些模組，不能在 DllMain 做，
//     改在背景執行緒裝（稍晚一點無妨，動態文字仍會被攔到）。
#include "hooks.hpp"
#include "common/config.hpp"
#include <MinHook.h>

namespace le {
HMODULE g_self = nullptr;

bool create_hook(const char* module, const char* fn, void* detour, void** original) {
    HMODULE h = GetModuleHandleA(module);
    if (!h) h = LoadLibraryA(module);
    if (!h) { le::log("找不到模組 %s\n", module); return false; }
    void* target = reinterpret_cast<void*>(GetProcAddress(h, fn));
    if (!target) { le::log("找不到 %s!%s\n", module, fn); return false; }
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s != MH_OK) { le::log("MH_CreateHook %s 失敗: %d\n", fn, s); return false; }
    return true;
}
}  // namespace le

// 背景裝文字/字型/locale hook（會 LoadLibrary gdi32/user32，故不在 DllMain 做）
static DWORD WINAPI init_text_thread(LPVOID) {
    le::install_locale_hooks();
    le::install_text_hooks();
    MH_EnableHook(MH_ALL_HOOKS);  // 一併啟用（行程 hook 已在 DllMain 啟用過，重複無害）
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    le::log("文字/字型 hook 安裝完成，cp=%d, 行程=%ls\n", le::codepage(), self);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        le::g_self = inst;
        DisableThreadLibraryCalls(inst);
        if (MH_Initialize() == MH_OK) {
            // 行程建立 hook：DllMain 內同步裝好 -> 主執行緒尚未跑 -> 攔得到後續子行程建立
            le::install_process_hooks();
            MH_EnableHook(MH_ALL_HOOKS);
            le::log("行程 hook 已早期安裝 pid=%lu\n", GetCurrentProcessId());
            HANDLE t = CreateThread(nullptr, 0, init_text_thread, nullptr, 0, nullptr);
            if (t) CloseHandle(t);
        } else {
            le::log("MH_Initialize 失敗\n");
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
