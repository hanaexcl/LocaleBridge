# LocaleBridge

一個從頭重寫的 **per-process 語系模擬器**（system region / locale emulator），
用途：在不改變系統地區設定的情況下，讓單一程式以指定的 ANSI code page（預設
GBK / 936、zh-CN）執行，正確顯示簡體中文。設計參考
[InWILL/Locale_Remulator](https://github.com/InWILL/Locale_Remulator)，但：

- 原生支援 **x64 與 Windows 11**。
- **修好了「啟動器偵測不到遊戲」的問題**，而且做法跟直覺相反：不是去補
  `FindWindowA` 的 hook，而是**刻意完全不 hook 視窗類別相關函式**。

  根因：LR 會 hook `RegisterClassExA`（把遊戲註冊的中文視窗類別名轉成 936），
  但沒 hook `FindWindowA`（啟動器搜尋時仍用系統 950 解）—— 註冊端轉了、搜尋端
  沒轉，兩邊對不上，啟動器就找不到遊戲視窗、按鈕卡在「開始遊戲」。
  本專案把語系模擬只做在 **code page / 字型** 這層，`RegisterClass` /
  `CreateWindow` / `FindWindow` 一律不碰，讓註冊端與搜尋端永遠用同一套規則 ——
  偵測天生一致，也順便避開 hook 視窗函式的 A/W 訊息編碼地雷。

它是編碼相容工具，不修改任何目標程式的檔案、不碰遊戲邏輯、不做任何作弊或
反偵測用途。

## 運作原理

```
LocaleLoader.exe  target.exe [args...]
   └─ CreateProcess(target, CREATE_SUSPENDED)
   └─ 注入對應位元的 LocaleHook*.dll
   └─ ResumeThread
        target.exe (已被 hook)
          GetACP()/GetLocaleInfoA()      -> 回報模擬的 936 / zh-CN
          MultiByteToWideChar(CP_ACP..)  -> 把 CP_ACP 重導成 936（修遊戲內文字）
          CreateFontA()/TextOutA()       -> 字型 charset 設 GB2312、用 936 轉字串
          CreateProcessA/W()             -> 對子行程再注入（含跨位元委派）
          (RegisterClass / FindWindow    -> 刻意不 hook，見上方說明)
```

`LocaleHook.dll` 用 [MinHook](https://github.com/TsudaKageyu/minhook) 做 inline hook。

## 設定

透過環境變數（會自動被子行程繼承）：

| 變數 | 預設 | 說明 |
|------|------|------|
| `LOCALEBRIDGE_CP` | `936` | 模擬的 ANSI code page |
| `LOCALEBRIDGE_LCID` | `0x0804` | 模擬的 LCID（zh-CN） |
| `LOCALEBRIDGE_LOG` | 空 | 設任意值則輸出 debug log 到 `OutputDebugString` |

## 建置

本地要 CMake ≥ 3.21 + MSVC。**若無本地 C++ 工具，直接用內附的 GitHub Actions**：
fork/push 後在 Actions 下載 `LocaleBridge` artifact，內含 x86/x64 兩套。

```powershell
cmake -B build-x64 -A x64  && cmake --build build-x64 --config Release
cmake -B build-x86 -A Win32 && cmake --build build-x86 --config Release
```

產物：`LocaleHook32.dll`、`LocaleHook64.dll`、`LocaleLoader32.exe`、`LocaleLoader64.exe`
（四個放同一資料夾）。

## 使用

以夢平台為例（x86 啟動器 → x64 遊戲）：

```powershell
LocaleLoader32.exe "D:\dianhun\mpl\夢平台.exe"
```

x86 loader 會 hook 啟動器，並在它產生 x64 遊戲子行程時，自動委派
`LocaleLoader64.exe` 對該 x64 行程注入 `LocaleHook64.dll`。

## 授權

MIT。MinHook 為 BSD-2-Clause。
