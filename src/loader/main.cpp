// LocaleLoader —— 啟動目標並注入 LocaleHook，之後常駐監看作為「後備」：
// 正常情況下遊戲本體會在建立當下就被 hook 傳遞注入（早期、SecureEngine 上線前）。
// 萬一早期注入沒趕上，watcher 會在寬限期後、且確認 DLL 尚未載入時，才從外部補注入。
//
//   LocaleLoader32.exe  <target.exe> [args...]     一般用法（啟動 + 常駐監看）
//   LocaleLoaderNN.exe  --inject <pid> <tid>       委派：對 suspended 行程排 APC（不 resume）
//   LocaleLoaderNN.exe  --inject-running <pid>     委派：對執行中行程注入（已載入則略過）
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <set>
#include <map>
#include "common/config.hpp"
#include "common/inject.hpp"

namespace {

const wchar_t* WATCH_NAMES[] = {L"msango.bin"};

std::wstring hook_dll_path() {
    return le::module_dir(nullptr) + L"\\" + le::hook_dll_this_arch();
}

int usage() {
    MessageBoxW(nullptr,
                L"用法:\n  LocaleLoader.exe <目標程式.exe> [參數...]\n\n"
                L"例:\n  LocaleLoader32.exe \"D:\\dianhun\\mpl\\夢平台.exe\"",
                L"LocaleBridge", MB_ICONINFORMATION);
    return 1;
}

bool ieq(const std::wstring& a, const wchar_t* b) { return _wcsicmp(a.c_str(), b) == 0; }

bool is_watch_target(const std::wstring& exe) {
    for (auto n : WATCH_NAMES) if (ieq(exe, n)) return true;
    return false;
}

// 該行程是否已載入我們的 hook DLL（只在同位元下可靠；跨位元交給同位元 loader 判斷）。
bool has_hook_loaded(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{sizeof(me)};
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do { if (wcsstr(me.szModule, L"LocaleHook")) { found = true; break; } }
        while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

bool run_and_wait(const std::wstring& cmdline, DWORD ms) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring c = cmdline;
    if (!CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, ms);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
}

// 對「執行中」行程注入（位元自適應）：同位元直接注入，跨位元委派同位元 loader。
void inject_running_pid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                           PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) { le::log("watcher OpenProcess pid=%lu 失敗 err=%lu\n", pid, GetLastError()); return; }
    bool child64 = le::process_is_64bit(h);
    bool self64 = sizeof(void*) == 8;
    if (child64 == self64) {
        if (has_hook_loaded(pid)) { le::log("watcher pid=%lu 已載入，略過\n", pid); CloseHandle(h); return; }
        bool ok = le::inject_running(h, hook_dll_path());
        le::log("watcher 同位元注入 pid=%lu %s\n", pid, ok ? "OK" : "失敗");
    } else {
        std::wstring loader = le::module_dir(nullptr) +
            (child64 ? L"\\LocaleLoader64.exe" : L"\\LocaleLoader32.exe");
        run_and_wait(L"\"" + loader + L"\" --inject-running " + std::to_wstring(pid), 15000);
        le::log("watcher 跨位元委派 pid=%lu -> %ls\n", pid, loader.c_str());
    }
    CloseHandle(h);
}

// 常駐監看：目標行程出現後給一段寬限期讓早期注入完成，逾期仍未注入才後備補注入。
void watch_loop() {
    const ULONGLONG GRACE_MS = 3000;
    std::set<DWORD> done;
    std::map<DWORD, ULONGLONG> first_seen;
    done.insert(GetCurrentProcessId());
    bool seen = false;
    int empty_ticks = 0;
    for (;;) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W e{sizeof(e)};
        int alive = 0;
        if (Process32FirstW(snap, &e)) {
            do {
                if (!is_watch_target(e.szExeFile)) continue;
                ++alive; seen = true;
                DWORD pid = e.th32ProcessID;
                if (done.count(pid)) continue;
                ULONGLONG now = GetTickCount64();
                auto it = first_seen.find(pid);
                if (it == first_seen.end()) {
                    first_seen[pid] = now;
                    le::log("watcher 發現 %ls pid=%lu，等待早期注入…\n", e.szExeFile, pid);
                    continue;
                }
                if (now - it->second < GRACE_MS) continue;  // 給早期注入時間
                done.insert(pid);
                le::log("watcher 寬限期滿，後備處理 pid=%lu\n", pid);
                inject_running_pid(pid);
            } while (Process32NextW(snap, &e));
        }
        CloseHandle(snap);
        if (seen && alive == 0) { if (++empty_ticks > 100) break; } else empty_ticks = 0;
        Sleep(20);
    }
    le::log("watcher 結束\n");
}

// --- 委派模式 ---
int do_inject(DWORD pid, DWORD tid) {  // 對 suspended 行程排 APC，不 resume
    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                               PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    HANDLE hThread = OpenThread(THREAD_SET_CONTEXT, FALSE, tid);
    if (!hProc || !hThread) {
        le::log("--inject OpenProcess/Thread 失敗 pid=%lu tid=%lu err=%lu\n", pid, tid, GetLastError());
        return 1;
    }
    bool ok = le::queue_loadlibrary_apc(hProc, hThread, hook_dll_path());
    le::log("--inject pid=%lu tid=%lu 排 APC %s\n", pid, tid, ok ? "OK" : "失敗");
    CloseHandle(hThread); CloseHandle(hProc);
    return ok ? 0 : 1;
}

int do_inject_running(DWORD pid) {  // 對執行中行程注入（已載入則略過）
    if (has_hook_loaded(pid)) { le::log("--inject-running pid=%lu 已載入，略過\n", pid); return 0; }
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                           PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) { le::log("--inject-running OpenProcess pid=%lu 失敗 err=%lu\n", pid, GetLastError()); return 1; }
    bool ok = le::inject_running(h, hook_dll_path());
    le::log("--inject-running pid=%lu %s\n", pid, ok ? "OK" : "失敗");
    CloseHandle(h);
    return ok ? 0 : 1;
}

// --- 一般模式：啟動目標 -> 注入 -> resume -> 常駐監看 ---
int do_launch(const std::wstring& cmdline) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmdline;
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        MessageBoxW(nullptr, (L"無法啟動:\n" + cmdline).c_str(), L"LocaleBridge", MB_ICONERROR);
        return 1;
    }
    bool child64 = le::process_is_64bit(pi.hProcess);
    bool self64 = sizeof(void*) == 8;
    le::log("launch pid=%lu child64=%d self64=%d cmd=%ls\n", pi.dwProcessId, child64, self64, cmdline.c_str());
    if (child64 == self64) {
        le::queue_loadlibrary_apc(pi.hProcess, pi.hThread, hook_dll_path());
    } else {
        std::wstring sibling = le::module_dir(nullptr) +
            (child64 ? L"\\LocaleLoader64.exe" : L"\\LocaleLoader32.exe");
        run_and_wait(L"\"" + sibling + L"\" --inject " + std::to_wstring(pi.dwProcessId) + L" " +
                     std::to_wstring(GetThreadId(pi.hThread)), 15000);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);

    watch_loop();
    return 0;
}

std::wstring join_args(int argc, wchar_t** argv, int start) {
    std::wstring out;
    for (int i = start; i < argc; ++i) {
        std::wstring a = argv[i];
        bool q = a.find(L' ') != std::wstring::npos && a.front() != L'"';
        if (!out.empty()) out += L' ';
        out += q ? (L'"' + a + L'"') : a;
    }
    return out;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return usage();
    std::wstring mode = argv[1];
    if (mode == L"--inject") {
        if (argc < 4) return usage();
        return do_inject(_wtoi(argv[2]), _wtoi(argv[3]));
    }
    if (mode == L"--inject-running") {
        if (argc < 3) return usage();
        return do_inject_running(_wtoi(argv[2]));
    }
    return do_launch(join_args(argc, argv, 1));
}
