using System.Globalization;
using System.Windows.Markup;
using Microsoft.Win32;

namespace LocaleBridge;

/// <summary>
/// UI 字串表。加一個語言 = 在 Codes/Names 加一欄，並在每個項目的陣列加同位置的字串。
/// 語言存 HKCU\Software\LocaleBridge\Language；沒存過就看系統 UI 語言。
/// </summary>
public static class Lang
{
    public static readonly string[] Codes = { "zh-TW", "en" };
    public static readonly string[] Names = { "繁體中文", "English" };

    public static int Index { get; private set; } = Detect();

    private const string Key = @"Software\LocaleBridge";

    private static int Detect()
    {
        var saved = Registry.CurrentUser.OpenSubKey(Key)?.GetValue("Language") as string;
        int i = Array.IndexOf(Codes, saved);
        if (i >= 0) return i;
        return CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "zh" ? 0 : 1;
    }

    public static void Set(int index)
    {
        Index = index;
        using var k = Registry.CurrentUser.CreateSubKey(Key);
        k.SetValue("Language", Codes[index]);
    }

    public static string T(string key) => S.TryGetValue(key, out var v) ? v[Index] : key;
    public static string T(string key, params object[] args) => string.Format(T(key), args);

    private static readonly Dictionary<string, string[]> S = new()
    {
        ["HeaderSub"] = new[] { "以指定語系啟動任何應用程式 — 不改變系統地區設定",
                                "Run any app in a chosen locale — without changing system settings" },
        ["SwitchTo"] = new[] { "English", "繁體中文" },

        ["Profiles"] = new[] { "語系設定檔", "Locale profiles" },
        ["NewProfile"] = new[] { "＋ 新增設定檔", "＋ New profile" },
        ["Delete"] = new[] { "刪除", "Delete" },

        ["AddTitle"] = new[] { "新增設定檔", "New profile" },
        ["EditTitleFmt"] = new[] { "編輯設定檔：{0}", "Edit profile: {0}" },
        ["EditHint"] = new[] { "① 選一個語言 → ② 需要的話改個好記的名稱 → ③ 儲存。",
                               "① Pick a language → ② rename it if you like → ③ save." },
        ["Step1"] = new[] { "① 選擇語言", "① Choose a language" },
        ["Step2"] = new[] { "② 設定檔名稱", "② Profile name" },
        ["Advanced"] = new[] { "進階：自訂編碼", "Advanced: custom encoding" },
        ["LcidHex"] = new[] { "LCID (16 進位)", "LCID (hex)" },
        ["Save"] = new[] { "儲存設定檔", "Save profile" },
        ["Cancel"] = new[] { "取消", "Cancel" },

        ["MenuTitle"] = new[] { "檔案總管右鍵選單", "Explorer context menu" },
        ["MenuHint"] = new[] { "安裝後，對任何 .exe 按右鍵即可選「用 LocaleBridge 啟動」→ 選一個設定檔來啟動。（Windows 11 在「顯示其他選項」裡）",
                               "Once installed, right-click any .exe → “Launch with LocaleBridge” → pick a profile. (On Windows 11 it lives under “Show more options”.)" },
        ["MenuInstall"] = new[] { "安裝 / 更新右鍵選單", "Install / update context menu" },
        ["MenuRemove"] = new[] { "移除", "Remove" },
        ["MenuStatusOn"] = new[] { "狀態：已安裝", "Status: installed" },
        ["MenuStatusOff"] = new[] { "狀態：未安裝", "Status: not installed" },
        ["MenuDone"] = new[] { "右鍵選單已安裝／更新。", "Context menu installed / updated." },
        ["ShellVerb"] = new[] { "用 LocaleBridge 啟動", "Launch with LocaleBridge" },

        ["LaunchTitle"] = new[] { "立即啟動", "Launch now" },
        ["LaunchHint"] = new[] { "用左側選取的設定檔啟動指定程式。",
                                 "Start a program with the profile selected on the left." },
        ["Browse"] = new[] { "瀏覽…", "Browse…" },
        ["LaunchBtn"] = new[] { "以選取的設定檔啟動", "Launch with selected profile" },
        ["ExeFilter"] = new[] { "應用程式 (*.exe)|*.exe|所有檔案 (*.*)|*.*",
                                "Applications (*.exe)|*.exe|All files (*.*)|*.*" },

        ["NeedName"] = new[] { "請輸入設定檔名稱。", "Enter a profile name." },
        ["BadCp"] = new[] { "Code Page 需為正整數，例如 936。", "Code page must be a positive integer, e.g. 936." },
        ["BadLcid"] = new[] { "LCID 需為 16 進位，例如 0804。", "LCID must be hexadecimal, e.g. 0804." },
        ["ConfirmDelFmt"] = new[] { "確定刪除設定檔「{0}」？", "Delete profile “{0}”?" },
        ["LoaderMissingFmt"] = new[] { "找不到 {0}，請確認它與本程式放在同一資料夾。",
                                       "{0} not found — it must sit in the same folder as this program." },
        ["LoaderMissingShortFmt"] = new[] { "找不到 {0}。", "{0} not found." },
        ["PickProfile"] = new[] { "請先在左側選一個設定檔。", "Select a profile on the left first." },
        ["PickTarget"] = new[] { "請選擇要啟動的程式。", "Choose a program to launch." },
        ["LaunchFailFmt"] = new[] { "啟動失敗：{0}", "Launch failed: {0}" },
    };
}

/// <summary>XAML 用：Text="{l:T Key}"。切換語言靠重建視窗，不做 INotify 綁定。</summary>
public sealed class TExtension : MarkupExtension
{
    public string Key { get; set; } = "";
    public TExtension() { }
    public TExtension(string key) => Key = key;
    public override object ProvideValue(IServiceProvider sp) => Lang.T(Key);
}
