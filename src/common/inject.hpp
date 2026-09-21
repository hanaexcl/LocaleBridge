// 行程注入小工具（header-only）：把 hook DLL 載入目標行程。
//
// 手法：Early-Bird APC —— 對「剛建立、仍 suspended」的目標主執行緒排一個
// QueueUserAPC(LoadLibraryW)，等 ResumeThread 後、目標進入點執行前就先載入我們的
// DLL。這樣才能在目標任何程式碼跑起來之前完成 hook。LoadLibraryW 在同位元行程間
// 位址一致，所以直接用本行程的位址即可（跨位元則委派同位元的 loader 處理）。
#pragma once
#include <windows.h>
#include <string>

namespace le {

// 取得某模組（exe 傳 nullptr / dll 傳自己的 HMODULE）所在資料夾，結尾不含反斜線。
inline std::wstring module_dir(HMODULE mod) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(mod, path, MAX_PATH);
    std::wstring s(path);
    size_t slash = s.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : s.substr(0, slash);
}

// 目標行程是否為 64 位元。用 IsWow64Process2（W10+），32/64 兩邊都能可靠判定。
inline bool process_is_64bit(HANDLE hProc) {
    USHORT processMachine = 0, nativeMachine = 0;
    if (IsWow64Process2(hProc, &processMachine, &nativeMachine)) {
        // processMachine==UNKNOWN 表示行程以原生位元執行；否則為 WOW64（32-bit）。
        return processMachine != IMAGE_FILE_MACHINE_I386;
    }
    // 極舊系統退路
    BOOL wow64 = FALSE;
    IsWow64Process(hProc, &wow64);
    return !wow64;
}

// 把寬字串寫進目標行程，回傳遠端位址（失敗回 nullptr）。
inline void* write_remote_wstr(HANDLE hProc, const std::wstring& s) {
    SIZE_T bytes = (s.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(hProc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return nullptr;
    if (!WriteProcessMemory(hProc, remote, s.c_str(), bytes, nullptr)) {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        return nullptr;
    }
    return remote;
}

// 對 suspended 的目標主執行緒排入 LoadLibraryW(dllPath) 的 APC（Early-Bird）。
// 呼叫端負責之後 ResumeThread。
inline bool queue_loadlibrary_apc(HANDLE hProc, HANDLE hThread, const std::wstring& dllPath) {
    void* remote = write_remote_wstr(hProc, dllPath);
    if (!remote) return false;
    auto loadlib = reinterpret_cast<PAPCFUNC>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (!loadlib) return false;
    return QueueUserAPC(loadlib, hThread, reinterpret_cast<ULONG_PTR>(remote)) != 0;
}

// 對「已初始化完成、正在執行」的目標用 CreateRemoteThread(LoadLibraryW) 注入。
inline bool inject_running(HANDLE hProc, const std::wstring& dllPath) {
    void* remote = write_remote_wstr(hProc, dllPath);
    if (!remote) return false;
    auto loadlib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (!loadlib) return false;
    HANDLE th = CreateRemoteThread(hProc, nullptr, 0, loadlib, remote, 0, nullptr);
    if (!th) return false;
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    return true;
}

}  // namespace le
