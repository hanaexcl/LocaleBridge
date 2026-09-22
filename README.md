# LocaleBridge

**繁體中文** · [English](README.en.md)

以**指定語系（ANSI code page / LCID）啟動任何 Windows 應用程式**，不必更改系統的「非
Unicode 程式語言」。適合在繁體或英文系統上正確顯示簡體、日文等以舊式 ANSI 編碼寫死的
程式（遊戲、老軟體）。

概念與 [Locale Emulator](https://github.com/xupefei/Locale-Emulator) /
[Locale_Remulator](https://github.com/InWILL/Locale_Remulator) 相同，但：

- 原生支援 **x64 與 Windows 11**，用 **Microsoft Detours** 注入。
- **可對任何 `.exe` 按右鍵**選擇語系設定檔啟動（檔案總管整合）。
- 附一個乾淨的**設定介面**管理設定檔與右鍵選單，介面本身支援**繁體中文 / English**。
- 針對「啟動器 + 受保護遊戲本體」的兩段式程式特別處理：**不 hook 視窗類別函式**，
  避免破壞啟動器對遊戲視窗的偵測（這是某些語系模擬器會造成「啟動器一直顯示未開始遊戲」
  的原因）。

> 這是編碼相容工具：只影響字元編碼的解讀，不修改任何目標程式的檔案。

---

## 元件

| 檔案 | 用途 |
|------|------|
| `LocaleBridge.exe` | 設定介面（管理設定檔、安裝右鍵選單、手動啟動） |
| `LocaleLoader64.exe` / `LocaleLoader32.exe` | 啟動器：以指定設定檔啟動目標並注入 hook |
| `LocaleHook64.dll` / `LocaleHook32.dll` | 被注入的 hook，負責語系模擬 |

四個檔（連同 GUI 的 .NET 執行檔）需放在**同一資料夾**。

---

## 使用方式

### 1. 設定介面
執行 `LocaleBridge.exe`：

- **語系設定檔**：新增／編輯／刪除設定檔（名稱 + Code Page + LCID）。內建
  简体中文(936)、繁體中文(950)、日本語(932)、한국어(949) 快速預設。
- **右鍵選單**：按「安裝／更新右鍵選單」，之後對任何 `.exe` 按右鍵即可看到
  **「用 LocaleBridge 啟動」→（各設定檔）**。
  （Windows 11 傳統選單在「顯示其他選項」中。）
- **立即啟動**：選一個設定檔、瀏覽要啟動的程式，直接執行。
- **介面語言**：右上角按鈕可在**繁體中文 / English** 之間切換（記在
  `HKCU\Software\LocaleBridge\Language`；第一次開啟時依系統語言自動選擇）。

### 2. 右鍵啟動
安裝右鍵選單後，於檔案總管對 `.exe` 按右鍵 → 用 LocaleBridge 啟動 → 選語系。

### 3. 命令列
```
LocaleLoader64.exe --profile "简体中文 (GBK)" "D:\game\game.exe"
LocaleLoader64.exe "D:\game\game.exe"        # 不指定則用預設 936 / zh-CN
```
啟動器有 `requireAdministrator`，需要管理員權限的目標會自動觸發 UAC。

---

## 運作原理

```
LocaleLoader ──(Detours: DetourCreateProcess/UpdateProcessWithDll)──▶ 目標.exe（注入 LocaleHook）
   設定檔的 CodePage/LCID 透過環境變數傳入，子行程自動繼承
        LocaleHook 在目標內：
          GetACP / GetLocaleInfo …          → 回報設定的 code page / LCID
          MultiByteToWideChar(CP_ACP,…)     → 以設定的 code page 轉換（修正多數文字）
          CreateFontA / CreateFontIndirect… → 字型 charset 強制對應（GDI 依此解 DBCS）
          CreateProcess(A/W/InternalW)      → 對子行程傳遞注入（跨位元用 rundll32 helper）
```

- 注入靠 **Detours 改 import table**，DLL 在目標正常初始化流程中載入，對防竄改殼而言
  等同正常模組。
- 跨位元（例如 x86 啟動器產生 x64 遊戲）由 Detours 的 helper 機制處理。
- **不 hook** `RegisterClass` / `FindWindow`：讓視窗類別名在「註冊端」與「搜尋端」保持
  一致，啟動器才偵測得到遊戲視窗。

---

## 建置

需要 Windows + Visual Studio（MSVC，含 C++ 與 .NET 8 SDK）+ CMake ≥ 3.21。

```powershell
# 原生（分別建 x64 / x86）
cmake -B build-x64 -A x64  ; cmake --build build-x64 --config Release
cmake -B build-x86 -A Win32; cmake --build build-x86 --config Release
# 設定介面（self-contained，使用者免裝 .NET）
dotnet publish gui/LocaleBridge.csproj -c Release -r win-x64 --self-contained -o publish_gui
```
把 `build-*/Release` 的四個原生檔複製到 `publish_gui`，整個資料夾即可散布。

**沒有本機工具鏈也可以**：本專案內含 GitHub Actions，push 後到 Actions 下載
`LocaleBridge` artifact（已打包 GUI + 執行期 + 四個原生檔）。

---

## 介面翻譯

所有介面字串集中在 [`gui/Lang.cs`](gui/Lang.cs) 的一張表，每個項目是一個依
`Lang.Codes` 順序排列的字串陣列。**要加一個語言**：在 `Codes` / `Names` 各加一欄，
再為每個項目補上同一位置的翻譯即可，不需要 .resx 或衛星組件。

---

## 相依與授權

- [Microsoft Detours](https://github.com/microsoft/Detours)（MIT）— 注入與 hook。
- 設定介面為 .NET 8 WPF（self-contained 發佈，使用者端免裝執行期）。

本專案採 MIT 授權。
