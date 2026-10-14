using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using Forms = System.Windows.Forms;

namespace GestureControl.Desktop;

public partial class MainWindow : Window
{
    public ObservableCollection<ProfileRow> Rows { get; } = [];
    private readonly string settingsPath;
    private string legacyPort = "";
    private sealed record CameraChoice(string? Serial, string Label);
    private readonly WatchService service = new();
    private readonly SemaphoreSlim commands = new(1, 1);
    private readonly Forms.NotifyIcon tray;
    private readonly Forms.ToolStripMenuItem trayToggle;
    private readonly Queue<string> logLines = new();
    private bool loading = true, busy, exiting, loadFailed, balloonShown, paused, dirty;

    public MainWindow(string settingsPath, string? initialPort)
    {
        this.settingsPath = settingsPath;
        InitializeComponent();
        DataContext = this;
        SettingsPathText.Text = settingsPath;
        var fallback = new DesktopSettings("", ProfileSettings.Load(Path.Combine(AppContext.BaseDirectory, "profiles.example.json")));
        var settings = fallback;
        try { if (File.Exists(settingsPath)) settings = DesktopSettings.Load(settingsPath); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException or JsonException)
        {
            loadFailed = true;
            ErrorText.Text = "保存済み設定を読み込めませんでした。内容を確認して保存するまで開始できません。 " + e.Message;
        }
        AddRow(settings.Profiles.Default, true);
        foreach (var profile in settings.Profiles.Applications) AddRow(profile);
        ProfileGrid.SelectedIndex = 0;
        legacyPort = initialPort ?? settings.Port;
        RefreshCameras(settings.DeviceSerial);
        SaveText.Text = File.Exists(settingsPath) ? "保存済み設定を読み込みました。" : "開始または保存時に、ユーザーフォルダーへ設定を保存します。";
        if (loadFailed) SaveText.Text = "読み込み失敗。下の設定は初期値です。元ファイルは変更していません。";
        service.StatusChanged = status => OnUi(() => ShowStatus(status));
        service.Log = message => OnUi(() => AppendLog(message));
        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add("設定を開く", null, (_, _) => OnUi(RestoreWindow));
        trayToggle = new Forms.ToolStripMenuItem("開始", null, (_, _) => OnUi(async () => await ToggleAsync()));
        menu.Items.Add(trayToggle);
        menu.Items.Add("通常のAlt＋左右へ戻す", null, (_, _) => OnUi(async () => await StandaloneAsync()));
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add("終了", null, (_, _) => OnUi(async () => await ExitAsync()));
        tray = new Forms.NotifyIcon
        {
            Icon = System.Drawing.SystemIcons.Application,
            Text = "AtomS3R Gesture — 未開始", ContextMenuStrip = menu, Visible = true
        };
        tray.DoubleClick += (_, _) => OnUi(RestoreWindow);
        loading = false;

        UpdateControls();
    }
    private void OnUi(Action action)
    {
        if (!Dispatcher.HasShutdownStarted) Dispatcher.BeginInvoke(action);
    }
    private void AddRow(ShortcutProfile profile, bool isDefault = false)
    {
        var row = new ProfileRow(profile, isDefault);
        row.PropertyChanged += (_, _) => MarkDirty();
        Rows.Add(row);
    }
    private void MarkDirty()
    {
        if (loading) return;
        dirty = true;
        SaveText.Text = "未保存の変更があります。保存するまで動作中の設定には反映されません。";
        Title = "AtomS3R Gesture *";
    }
    private void RefreshCameras(string? selected = null)
    {
        selected ??= (CameraBox.SelectedItem as CameraChoice)?.Serial;
        var choices = new List<CameraChoice> { new(null, "自動検出") };
        try
        {
            choices.AddRange(WindowsHidTransport.FindCameras().Select(c => new CameraChoice(c.SerialNumber, c.DisplayName)));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        { ErrorText.Text = "カメラを検索できませんでした: " + e.Message; }
        if (!string.IsNullOrEmpty(selected) && choices.All(c => c.Serial != selected))
            choices.Add(new(selected, $"未接続 · {selected}"));
        CameraBox.ItemsSource = choices;
        CameraBox.SelectedItem = choices.FirstOrDefault(c => c.Serial == selected) ?? choices[0];
    }
    private DesktopSettings ReadEditor()
    {
        ProfileGrid.CommitEdit(DataGridEditingUnit.Cell, true);
        ProfileGrid.CommitEdit(DataGridEditingUnit.Row, true);
        GestureGrid.CommitEdit(DataGridEditingUnit.Cell, true);
        GestureGrid.CommitEdit(DataGridEditingUnit.Row, true);
        var fallback = Rows.Single(r => r.IsDefault);
        var json = JsonSerializer.Serialize(new
        {
            version = 2,
            @default = new { enabled = fallback.Enabled, bindings = fallback.ReadBindings() },
            applications = Rows.Where(r => !r.IsDefault).Select(r => new
            { executable = r.Executable.Trim(), enabled = r.Enabled, bindings = r.ReadBindings() })
        });
        return new(legacyPort, ProfileSettings.Parse(json))
        { DeviceSerial = DesktopSettings.ValidateSerial((CameraBox.SelectedItem as CameraChoice)?.Serial) };
    }
    private async Task PersistAsync(DesktopSettings settings)
    {
        await Task.Run(() => settings.Save(settingsPath));
        loadFailed = false;
        dirty = false;
        Title = "AtomS3R Gesture";
        SaveText.Text = "保存しました。";
    }
    private async Task StartAsync(DesktopSettings settings)
    {
        StatusText.Text = "カメラへ接続しています…";
        ActiveText.Text = "接続後、前面アプリを確認して割り当てを適用します。";
        paused = false;
        await service.StartAsync(settings.Profiles, () =>
        {
            var current = WindowsForeground.Current();
            return current?.ProcessId == (uint)Environment.ProcessId ? null : current;
        }, () =>
        {
            var device = ControlDevice.Open(serial: settings.DeviceSerial);
            device.Log = line =>
            {
                OnUi(() => AppendLog(line));
            };
            return device;
        });
    }
    private async Task RunActionAsync(Func<Task> action)
    {
        if (busy || exiting) return;
        busy = true;
        UpdateControls();
        await commands.WaitAsync();
        try { ErrorText.Text = ""; await action(); }
        catch (Exception e)
        {
            ErrorText.Text = "操作できませんでした: " + e.Message;
            AppendLog(e.Message);
        }
        finally { commands.Release(); busy = false; UpdateControls(); }
    }
    private Task ToggleAsync() => RunActionAsync(async () =>
    {
        if (service.IsRunning)
        {
            await service.StopAsync();
            paused = true;
            StatusText.Text = "一時停止";
            ActiveText.Text = "入力停止を要求しました。通信断の場合も最後の通信から2秒で停止します。";
            tray.Text = "AtomS3R Gesture — 一時停止";
        }
        else
        {
            if (loadFailed) throw new InvalidDataException("読み込みに失敗した設定を確認し、保存してから開始してください。");
            var settings = ReadEditor();
            await PersistAsync(settings);
            await StartAsync(settings);
        }
    });
    private Task SaveAsync() => RunActionAsync(async () =>
    {
        var running = service.IsRunning;
        var settings = ReadEditor();
        await PersistAsync(settings);
        if (running)
        {
            await StartAsync(settings);
            SaveText.Text = "保存しました。新しい設定で前面アプリを確認しています。";
        }
    });
    private Task StandaloneAsync() => RunActionAsync(async () =>
    {
        var serial = (CameraBox.SelectedItem as CameraChoice)?.Serial;
        await service.StopAsync();
        await Task.Run(() =>
        {
            using var device = ControlDevice.Open(serial: serial);
            device.Identify(); device.Standalone();
        });
        paused = false;
        StatusText.Text = "通常モード — Alt＋← / Alt＋→";
        ActiveText.Text = "アプリを終了しても、カメラ単体で通常のショートカットを使えます。";
        tray.Text = "AtomS3R Gesture — 通常モード";
    });
    private void ShowStatus(WatchStatus status)
    {
        if (exiting) return;
        (StatusText.Text, ActiveText.Text) = status.State switch
        {
            WatchState.WaitingForFocus => ("接続済み — 切替先を確認中", status.Executable ?? "前面アプリを確認しています。"),
            WatchState.ForegroundUnavailable => (IsActive ? "設定画面を操作中 — 入力停止" : "前面アプリを取得できないため入力停止", "メモ帳やブラウザーへ切り替えると自動で再開します。"),
            WatchState.Applied => (status.Profile!.Enabled ? "接続済み — アプリ別切り替え中" : "接続済み — このアプリでは無効",
                $"{status.Executable}   割り当て {status.Profile.Bindings.ActiveEntries.Count} 件"),
            WatchState.Disconnected => ("未接続 — 自動再接続を待っています", status.Error ?? "カメラのUSB接続を確認してください。"),
            WatchState.Faulted => ("監視を終了しました", status.Error ?? "開始し直してください。"),
            WatchState.Stopped => ("入力停止を確認しました", "再開するには開始ボタンを押してください。"),
            _ => ("入力停止", "")
        };
        var tooltip = "AtomS3R Gesture — " + (status.State == WatchState.Applied ? status.Executable : StatusText.Text);
        tray.Text = tooltip.Length > 63 ? tooltip[..63] : tooltip;
        UpdateControls();
    }
    private void UpdateControls()
    {
        var available = !busy && !exiting;
        ToggleButton.Content = service.IsRunning ? "一時停止" : paused ? "再開" : "開始";
        ToggleButton.IsEnabled = available && (!loadFailed || service.IsRunning);
        trayToggle.Text = ToggleButton.Content.ToString();
        trayToggle.Enabled = ToggleButton.IsEnabled;
        CameraBox.IsEnabled = RefreshButton.IsEnabled = available && !service.IsRunning;
        SaveButton.IsEnabled = StandaloneButton.IsEnabled = AddButton.IsEnabled = RemoveButton.IsEnabled = ProfileGrid.IsEnabled = MappingEditor.IsEnabled = available;
    }
    private void AppendLog(string message)
    {
        logLines.Enqueue($"{DateTime.Now:HH:mm:ss} {message}");
        while (logLines.Count > 100) logLines.Dequeue();
        LogText.Text = string.Join(Environment.NewLine, logLines);
        LogText.ScrollToEnd();
    }
    public void RestoreWindow() { Show(); WindowState = WindowState.Normal; Activate(); }
    private void OnClosing(object? sender, CancelEventArgs e)
    {
        if (exiting) return;
        e.Cancel = true; Hide();
        if (!balloonShown)
        {
            tray.ShowBalloonTip(3000, "AtomS3R Gesture", "通知領域に収納しました。アイコンから設定・一時停止・終了を操作できます。", Forms.ToolTipIcon.Info);
            balloonShown = true;
        }
    }
    private async Task ExitAsync()
    {
        if (exiting) return;
        exiting = true;
        UpdateControls();
        await commands.WaitAsync();
        var finished = false;
        try
        {
            // Wait for any in-flight save/start first; a failed save must not
            // discard edits just because Exit was clicked while it was busy.
            if (dirty)
            {
                RestoreWindow();
                var choice = MessageBox.Show(this, "変更を保存して終了しますか？", "未保存の設定", MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
                if (choice == MessageBoxResult.Cancel) return;
                if (choice == MessageBoxResult.Yes) await PersistAsync(ReadEditor());
            }
            await service.DisposeAsync();
            finished = true;
        }
        catch (Exception e) { ErrorText.Text = "終了できませんでした: " + e.Message; }
        finally
        {
            commands.Release();
            if (finished)
            {
                tray.Visible = false; tray.ContextMenuStrip?.Dispose(); tray.Dispose();
                Application.Current.Shutdown();
            }
            else { exiting = false; UpdateControls(); }
        }
    }
    public void StopForSessionEnd()
    {
        exiting = true;
        service.StopAsync().Wait(TimeSpan.FromSeconds(3));
        tray.Visible = false; tray.Dispose();
    }
    private async void ToggleClick(object sender, RoutedEventArgs e) => await ToggleAsync();
    private async void SaveClick(object sender, RoutedEventArgs e) => await SaveAsync();
    private async void StandaloneClick(object sender, RoutedEventArgs e) => await StandaloneAsync();
    private async void ExitClick(object sender, RoutedEventArgs e) => await ExitAsync();
    private void CameraChanged(object sender, SelectionChangedEventArgs e) => MarkDirty();
    private async void RefreshClick(object sender, RoutedEventArgs e) => await RunActionAsync(() => { RefreshCameras(); return Task.CompletedTask; });
    private void AddClick(object sender, RoutedEventArgs e)
    {
        AddRow(new("app.exe", Binding.Left, Binding.Right, true));
        ProfileGrid.SelectedItem = Rows[^1]; ProfileGrid.ScrollIntoView(Rows[^1]); MarkDirty();
    }
    private void AddGestureClick(object sender, RoutedEventArgs e)
    {
        if (ProfileGrid.SelectedItem is not ProfileRow row) return;
        var path = new[] { "RD", "RDL" }.Concat(GestureChoice.All.Select(c => c.Path))
            .FirstOrDefault(p => row.Gestures.All(g => g.Path != p));
        if (path is null) return;
        row.AddGesture(path); GestureGrid.SelectedItem = row.Gestures[^1];
        GestureGrid.ScrollIntoView(row.Gestures[^1]);
    }
    private void RemoveGestureClick(object sender, RoutedEventArgs e)
    {
        if (ProfileGrid.SelectedItem is ProfileRow row && GestureGrid.SelectedItem is GestureRow gesture)
            row.RemoveGesture(gesture);
    }
    private void RemoveClick(object sender, RoutedEventArgs e)
    {
        if (ProfileGrid.SelectedItem is ProfileRow { IsDefault: false } row) { Rows.Remove(row); MarkDirty(); }
    }
}
