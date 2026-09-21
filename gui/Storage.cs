using Microsoft.Win32;

namespace LocaleBridge;

/// <summary>一個語系設定：名稱 + ANSI code page + LCID。</summary>
public sealed class Profile
{
    public string Name { get; set; } = "";
    public int CodePage { get; set; } = 936;
    public int Lcid { get; set; } = 0x0804;

    public string Summary => $"CP {CodePage} · LCID 0x{Lcid:X4}";

    // 常見預設
    public static readonly (string Name, int Cp, int Lcid)[] Presets =
    {
        ("简体中文 (GBK)",      936, 0x0804),
        ("繁體中文 (Big5)",     950, 0x0404),
        ("日本語 (Shift-JIS)",  932, 0x0411),
        ("한국어 (EUC-KR)",     949, 0x0412),
    };
}

/// <summary>profiles 存於 HKCU\Software\LocaleBridge\Profiles\&lt;name&gt;（CodePage/LCID DWORD）。</summary>
public static class ProfileStore
{
    private const string Root = @"Software\LocaleBridge\Profiles";

    public static List<Profile> Load()
    {
        var list = new List<Profile>();
        using var root = Registry.CurrentUser.OpenSubKey(Root);
        if (root == null) return list;
        foreach (var name in root.GetSubKeyNames())
        {
            using var k = root.OpenSubKey(name);
            if (k == null) continue;
            list.Add(new Profile
            {
                Name = name,
                CodePage = (int)(k.GetValue("CodePage") ?? 936),
                Lcid = (int)(k.GetValue("LCID") ?? 0x0804),
            });
        }
        list.Sort((a, b) => string.Compare(a.Name, b.Name, StringComparison.Ordinal));
        return list;
    }

    public static void Save(Profile p)
    {
        using var k = Registry.CurrentUser.CreateSubKey($@"{Root}\{p.Name}");
        k.SetValue("CodePage", p.CodePage, RegistryValueKind.DWord);
        k.SetValue("LCID", p.Lcid, RegistryValueKind.DWord);
    }

    public static void Delete(string name)
    {
        using var root = Registry.CurrentUser.OpenSubKey(Root, writable: true);
        root?.DeleteSubKeyTree(name, throwOnMissingSubKey: false);
    }

    public static void EnsureDefaults()
    {
        if (Load().Count > 0) return;
        Save(new Profile { Name = "简体中文 (GBK)", CodePage = 936, Lcid = 0x0804 });
    }
}

/// <summary>
/// 右鍵選單：寫入 HKCU\Software\Classes\exefile\shell\LocaleBridge，用 ExtendedSubCommands
/// 串接每個 profile 的子項；點擊執行 LocaleLoader --profile "名稱" "%1"。全程 per-user，免管理員。
/// </summary>
public static class ContextMenu
{
    private const string Verb = @"Software\Classes\exefile\shell\LocaleBridge";

    public static bool IsInstalled()
    {
        using var k = Registry.CurrentUser.OpenSubKey(Verb);
        return k != null;
    }

    public static void Install(IEnumerable<Profile> profiles, string loaderPath, string iconPath)
    {
        Uninstall();
        using (var verb = Registry.CurrentUser.CreateSubKey(Verb))
        {
            verb.SetValue("MUIVerb", "用 LocaleBridge 啟動");
            verb.SetValue("Icon", $"{iconPath},0");
            verb.SetValue("Position", "Middle");
        }
        int i = 0;
        foreach (var p in profiles)
        {
            string sub = $@"{Verb}\ExtendedSubCommands\{i:D2}_{Sanitize(p.Name)}";
            using (var item = Registry.CurrentUser.CreateSubKey(sub))
                item.SetValue("MUIVerb", p.Name);
            using (var cmd = Registry.CurrentUser.CreateSubKey($@"{sub}\command"))
                cmd.SetValue("", $"\"{loaderPath}\" --profile \"{p.Name}\" \"%1\"");
            i++;
        }
    }

    public static void Uninstall()
    {
        using var classes = Registry.CurrentUser.OpenSubKey(@"Software\Classes\exefile\shell", writable: true);
        classes?.DeleteSubKeyTree("LocaleBridge", throwOnMissingSubKey: false);
    }

    private static string Sanitize(string s)
    {
        foreach (var c in Path.GetInvalidFileNameChars().Append('\\'))
            s = s.Replace(c, '_');
        return s;
    }
}
