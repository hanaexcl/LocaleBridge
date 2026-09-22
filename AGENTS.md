# AGENTS.md — LocaleBridge 專案脈絡(給 AI 接手用)

> 這份文件給「在新對話/新視窗接手本專案的 AI」快速掌握全貌與地雷。
> 人類使用說明看 [README.md](README.md)。repo:`hanaexcl/LocaleBridge`(原名 msg-translation,已改名,GitHub 自動轉址)。

## 這是什麼

一套 **per-process 語系模擬器**:以指定 ANSI code page / LCID 啟動任何 Windows 程式,
讓繁體/英文系統上的舊式 ANSI 程式(遊戲、老軟體)正確顯示簡體/日文等,**不改系統地區設定**。
概念同 Locale Emulator / Locale_Remulator(LR),但原生支援 x64 / Win11、修好了 LR 會破壞
「啟動器偵測遊戲」的問題、並附右鍵選單與設定介面。

**起源目標程式**:`D:\dianhun\mpl\夢平台.exe`(夢三國2)。行程樹:
`夢平台.exe(x86 小啟動器)` → `DHPlatform.bin(x86 登入器 UI)` → `DHProtect.bin(x86, Oreans
SecureEngine 保護殼)` → `MSango.bin(x64 遊戲本體)`。開發機系統為 zh-TW / ANSI code page 950。

## 檔案地圖

```
src/common/config.hpp     讀環境變數 LOCALEBRIDGE_CP / LOCALEBRIDGE_LCID(預設 936 / 0x0804)
src/common/convert.hpp    ANSI<->wide 小工具(目前 hook.cpp 未直接用)
src/hook/hook.cpp         注入到目標的 hook DLL 全部邏輯(Detours)
src/hook/exports.def      匯出 DetourFinishHelperProcess @1(跨位元 rundll32 helper 需要)
src/loader/main.cpp       LocaleLoader:啟動目標並注入;--profile 讀登錄檔設環境變數
src/loader/loader.manifest requireAdministrator(目標多為需管理員)
CMakeLists.txt            建 LocaleHook{32,64}.dll + LocaleLoader{32,64}.exe;FetchContent 抓 Detours 自編
gui/                      C# WPF 設定介面(net8.0-windows,self-contained)
  App.xaml(主題)  MainWindow.xaml(.cs)  Storage.cs(profiles + 右鍵選單登錄檔)  LocaleBridge.ico
  Lang.cs                   介面多語系:一張 key → 依 Lang.Codes 排序的字串陣列表 + {l:T Key}
                            markup extension;語言存 HKCU\Software\LocaleBridge\Language,
                            首次依系統 UI 語言。切換語言 = 重建 MainWindow(XAML 字串載入時求值)。
                            加語言:Codes/Names 加一欄 + 每個項目補同位置翻譯。
README.md / README.en.md    中英文說明(互相連結)
.github/workflows/build.yml  CI:建 C++(x86/x64)+ dotnet publish GUI → 打包 artifact;打 v* tag 自動發 Release
```

## 架構與資料流

1. 使用者(或右鍵選單)執行 `LocaleLoader64.exe --profile "<名稱>" "<target.exe>"`。
2. loader 從 `HKCU\Software\LocaleBridge\Profiles\<名稱>` 讀 `CodePage`/`LCID`(DWORD),
   `SetEnvironmentVariable` LOCALEBRIDGE_CP/LCID(子行程會繼承)。
3. loader 建立 target(suspended),用 **Detours** 把對應位元的 `LocaleHook*.dll` 注入,resume。
4. hook DLL 在每個行程的 `DllMain` 同步 attach hook;並 hook `CreateProcess*`,把注入**傳遞給子行程**
   (整棵樹都被 hook)。
5. hook 讓 `GetACP` 等回報模擬值、`MultiByteToWideChar(CP_ACP,…)` 用模擬 code page 轉、
   字型強制 GB2312 charset → 文字正確顯示。

## 關鍵設計決策(WHY —— 這些是踩出來的,別退回去)

- **注入引擎用 Microsoft Detours,不是 MinHook,更不是手動 APC/CreateRemoteThread。**
  Detours 靠改 import table 讓 DLL 在**正常初始化流程**載入 → 對 SecureEngine 是正常模組,不觸發
  防竄改。手動注入(尤其事後外部注入)會被 SecureEngine 偵測、直接終止遊戲(實測會當機)。
- **刻意不 hook `RegisterClass` / `FindWindow`。** 這是 LR 破壞「啟動器偵測遊戲」的根因:遊戲的
  中文視窗類別名(如「梦三国2 Online」)在註冊端被轉碼、啟動器搜尋端沒轉 → 對不上 → 按鈕卡在
  「開始遊戲」。兩端都不碰就一致,偵測正常。
- **CreateProcess 三個進入點全 hook**:`CreateProcessA` / `CreateProcessW` / `CreateProcessInternalW`,
  用 thread-local 深度旗標防同一次建立被重複處理。只 hook InternalW 不夠(夢平台→DHPlatform 走 A/W);
  `NtCreateUserProcess` 試過但**不需要**(LR 也沒 hook,已從程式移除)。
- **子行程注入依位元**:同位元用 `DetourUpdateProcessWithDll`(改 import table);跨位元(x86 保護
  loader 產生 x64 遊戲)用 `DetourProcessViaHelperW`,Detours 以另一位元的 `rundll32` 當 helper。
  helper 會載入本 DLL 並呼叫其 ordinal 1 = `DetourFinishHelperProcess`(故 exports.def 必須匯出它);
  `DllMain` 用 `DetourIsHelperProcess()` 早退。
- **文字修正的關鍵是 `MultiByteToWideChar(CP_ACP)` 重導**(遊戲走 D3D 自繪,不是 GDI TextOut),
  外加 `CreateFontA/IndirectA/ExA` 強制 GB2312 charset(GDI 依字型 charset 解 DBCS,故不需 hook TextOut)。
- **loader / 右鍵選單需要提權**:目標 manifest 是 requireAdministrator,loader 自帶 requireAdministrator
  會觸發 UAC。GUI 本身只寫 HKCU,asInvoker、免管理員。
- **右鍵選單為 per-user 靜態選單**(免 COM、免管理員):`HKCU\Software\Classes\exefile\shell\LocaleBridge`,
  父項設 `MUIVerb` + 空字串值 `subcommands` + 子鍵 `shell\<NN_名稱>\command`(串接子選單的正確寫法;
  用 `ExtendedSubCommands` 直接放項目**不會動作**)。父項與各子項都設 `Icon = "<LocaleBridge.exe>,0"`。

## 建置 / CI / 發布

- 本機:`cmake -B build-x64 -A x64 && cmake --build build-x64 --config Release`(x86 用 `-A Win32`);
  GUI:`dotnet publish gui/LocaleBridge.csproj -c Release -r win-x64 --self-contained -o publish_gui`。
- **無本機工具鏈就靠 CI**:push 後 GitHub Actions 產出 `LocaleBridge` artifact(GUI + 執行期 + 4 原生檔)。
- **發新版**:`git tag vX.Y.Z -m "…" && git push origin vX.Y.Z` → CI 自動打包 zip 並建 GitHub Release。

## 建置地雷(已解,別重犯)

- Detours v4.0.1 原始碼要用 **C++14 + `/permissive`** 編(C++20 嚴格模式下 `creatwth.cpp` 報 C2362)。
- `uimports.cpp` 被 `creatwth.cpp` `#include`,**不可**單獨列入編譯清單。
- `hook.cpp` 要自己 `#include <cstdio>/<cstring>`(config.hpp 已精簡不含)。
- C# 類別**別取名 `ContextMenu`**(與 WPF `System.Windows.Controls.ContextMenu` 撞名)→ 用 `ShellMenu`;
  `Storage.cs` 需 `using System.IO;`;App.xaml 資源 key 不可重複(brush 與 style 別同名)。
- `IsWow64Process2` 需 `_WIN32_WINNT=0x0A00`(CMake 已定義)。

## 執行期驗證(對夢平台)

用 `LocaleLoader32.exe "D:\dianhun\mpl\夢平台.exe"` 或右鍵啟動,確認四件事全過:
① 登入器文字翻譯 ② 遊戲內文字翻譯 ③ 遊戲正常啟動(未被 SecureEngine 中止)④ 平台偵測按鈕正常。
目前(v1.0.0)四項皆通過。debug log 已全部移除(乾淨運作);若要再排錯,得先臨時加回檔案 log。

## 開發機備註(push / gh)

- `gh` 在 `C:\Program Files\GitHub CLI\gh.exe`(不在 bash PATH,先 export)。
- **git push 常卡住**(系統 GCM 在無視窗環境彈 GUI):用單指令一次性帶 gh 認證,不要寫進 repo 設定:
  `git -C <repo> -c credential.helper='!"/c/Program Files/GitHub CLI/gh.exe" auth git-credential' push origin <ref>`
- 複合指令(`&&`/`;`、多行 commit message)在此環境常被權限提示擋 → 用單一 `git -C <repo> commit -aqm "…"`。
