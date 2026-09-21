using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace LocaleBridge;

public partial class MainWindow : Window
{
    private string? _editingOriginalName;

    public MainWindow()
    {
        InitializeComponent();
        BuildPresetChips();
        ProfileStore.EnsureDefaults();
        ReloadProfiles();
        RefreshMenuStatus();
    }

    private string BaseDir => AppContext.BaseDirectory;
    private string LoaderPath =>
        Path.Combine(BaseDir, Environment.Is64BitOperatingSystem ? "LocaleLoader64.exe" : "LocaleLoader32.exe");

    // ---- profiles ----
    private void ReloadProfiles(string? select = null)
    {
        var list = ProfileStore.Load();
        ProfileList.ItemsSource = list;
        if (list.Count == 0) { ClearEditor(); return; }
        var pick = list.FirstOrDefault(p => p.Name == select) ?? list[0];
        ProfileList.SelectedItem = pick;
    }

    private void ProfileList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ProfileList.SelectedItem is Profile p)
        {
            _editingOriginalName = p.Name;
            NameBox.Text = p.Name;
            CpBox.Text = p.CodePage.ToString();
            LcidBox.Text = p.Lcid.ToString("X4");
        }
    }

    private void ClearEditor()
    {
        _editingOriginalName = null;
        NameBox.Text = CpBox.Text = LcidBox.Text = "";
    }

    private void New_Click(object sender, RoutedEventArgs e)
    {
        ProfileList.SelectedItem = null;
        ClearEditor();
        NameBox.Text = "新設定檔";
        CpBox.Text = "936";
        LcidBox.Text = "0804";
        NameBox.Focus();
        NameBox.SelectAll();
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        string name = NameBox.Text.Trim();
        if (string.IsNullOrEmpty(name)) { Warn("請輸入設定檔名稱。"); return; }
        if (!int.TryParse(CpBox.Text.Trim(), out int cp) || cp <= 0) { Warn("Code Page 需為正整數，例如 936。"); return; }
        if (!int.TryParse(LcidBox.Text.Trim(), System.Globalization.NumberStyles.HexNumber, null, out int lcid))
        { Warn("LCID 需為 16 進位，例如 0804。"); return; }

        if (_editingOriginalName != null && _editingOriginalName != name)
            ProfileStore.Delete(_editingOriginalName);   // 更名 → 刪舊鍵

        ProfileStore.Save(new Profile { Name = name, CodePage = cp, Lcid = lcid });
        ReloadProfiles(name);
        if (ContextMenu.IsInstalled()) InstallMenuSilently();  // 讓右鍵選單同步
    }

    private void Delete_Click(object sender, RoutedEventArgs e)
    {
        if (ProfileList.SelectedItem is not Profile p) return;
        if (MessageBox.Show($"確定刪除設定檔「{p.Name}」？", "LocaleBridge",
                MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK) return;
        ProfileStore.Delete(p.Name);
        ReloadProfiles();
        if (ContextMenu.IsInstalled()) InstallMenuSilently();
    }

    private void BuildPresetChips()
    {
        foreach (var (nm, cp, lcid) in Profile.Presets)
        {
            var b = new Button
            {
                Content = nm,
                Style = (Style)FindResource("Ghost"),
                Margin = new Thickness(0, 0, 8, 8),
                Padding = new Thickness(12, 6, 12, 6),
                FontSize = 13,
            };
            b.Click += (_, _) =>
            {
                if (string.IsNullOrWhiteSpace(NameBox.Text)) NameBox.Text = nm;
                CpBox.Text = cp.ToString();
                LcidBox.Text = lcid.ToString("X4");
            };
            PresetPanel.Children.Add(b);
        }
    }

    // ---- 右鍵選單 ----
    private void InstallMenu_Click(object sender, RoutedEventArgs e)
    {
        if (!File.Exists(LoaderPath)) { Warn($"找不到 {Path.GetFileName(LoaderPath)}，請確認它與本程式放在同一資料夾。"); return; }
        InstallMenuSilently();
        RefreshMenuStatus();
        MessageBox.Show("右鍵選單已安裝／更新。", "LocaleBridge", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    private void InstallMenuSilently()
    {
        var exe = Process.GetCurrentProcess().MainModule?.FileName ?? Path.Combine(BaseDir, "LocaleBridge.exe");
        ContextMenu.Install(ProfileStore.Load(), LoaderPath, exe);
    }

    private void RemoveMenu_Click(object sender, RoutedEventArgs e)
    {
        ContextMenu.Uninstall();
        RefreshMenuStatus();
    }

    private void RefreshMenuStatus()
    {
        MenuStatus.Text = ContextMenu.IsInstalled() ? "狀態：已安裝" : "狀態：未安裝";
    }

    // ---- 立即啟動 ----
    private void Browse_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Filter = "應用程式 (*.exe)|*.exe|所有檔案 (*.*)|*.*" };
        if (dlg.ShowDialog() == true) TargetBox.Text = dlg.FileName;
    }

    private void Launch_Click(object sender, RoutedEventArgs e)
    {
        if (ProfileList.SelectedItem is not Profile p) { Warn("請先在左側選一個設定檔。"); return; }
        string target = TargetBox.Text.Trim();
        if (!File.Exists(target)) { Warn("請選擇要啟動的程式。"); return; }
        if (!File.Exists(LoaderPath)) { Warn($"找不到 {Path.GetFileName(LoaderPath)}。"); return; }
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = LoaderPath,
                Arguments = $"--profile \"{p.Name}\" \"{target}\"",
                UseShellExecute = true,   // 讓 LocaleLoader 的 requireAdministrator 觸發 UAC
            });
        }
        catch (Exception ex) { Warn("啟動失敗：" + ex.Message); }
    }

    private static void Warn(string msg) =>
        MessageBox.Show(msg, "LocaleBridge", MessageBoxButton.OK, MessageBoxImage.Warning);
}
