using GestureControl;

internal static class DesktopTests
{
    private const string Json = """
        {"version":1,"default":{"enabled":true,"left":"Alt+Left","right":"Alt+Right"},"applications":[
        {"executable":"notepad.exe","enabled":true,"left":"Ctrl+Shift+Tab","right":"F24"},
        {"executable":"mspaint.exe","enabled":false,"left":"None","right":"None"}]}
        """;
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    public static async Task RunAsync()
    {
        var profiles = ProfileSettings.Parse(Json);
        Check(ProfileSettings.Parse(profiles.ToJson()).ToJson() == profiles.ToJson(), "profile serialization roundtrip");
        foreach (var key in new[] { "None", "A", "Z", "0", "9", "F1", "F12", "F13", "F24", "PageUp", "PageDown", "Enter", "Left" })
            Check(Binding.Parse(Binding.Parse(key).ToShortcutString()) == Binding.Parse(key), $"key roundtrip {key}");
        for (int modifiers = 0; modifiers <= 255; ++modifiers)
        {
            var binding = new Binding((byte)modifiers, 43);
            Check(Binding.Parse(binding.ToShortcutString()) == binding, "all modifier bits roundtrip");
        }
        var directory = Path.Combine(Path.GetTempPath(), "gesture-settings-test-" + Guid.NewGuid().ToString("N"));
        var path = Path.Combine(directory, "settings.json");
        try
        {
            var first = new DesktopSettings("com10", profiles);
            first.Save(path);
            Check(DesktopSettings.Load(path).Port == "COM10", "saved normalized port");
            Check(DesktopSettings.Load(path).Profiles.ToJson() == profiles.ToJson(), "saved rules");
            var second = first with { Port = "COM11" };
            second.Save(path);
            Check(DesktopSettings.Load(path).Port == "COM11" && DesktopSettings.Load(path + ".bak").Port == "COM10", "atomic replacement and backup");
            try { (second with { Port = "COM1\nu" }).Save(path); throw new Exception("bad port accepted"); }
            catch (InvalidDataException) { }
            Check(DesktopSettings.Load(path).Port == "COM11", "invalid save preserves existing settings");
            Check(Directory.GetFiles(directory, "*.tmp").Length == 0, "no temporary file left behind");
        }
        finally { if (Directory.Exists(directory)) Directory.Delete(directory, true); }

        var note = new ForegroundApp(1, 1, "notepad.exe");
        var a = new Device(); var b = new Device();
        await using (var service = new WatchService())
        {
            await service.StartAsync(profiles, () => note, () => a);
            await a.Applied.Task.WaitAsync(TimeSpan.FromSeconds(5));
            Check(service.IsRunning, "worker started");
            await service.StartAsync(profiles, () => note, () =>
            {
                Check(a.Disposed && a.Stops > 0, "old worker must stop/close before replacement opens");
                return b;
            });
            await b.Applied.Task.WaitAsync(TimeSpan.FromSeconds(5));
            await service.StopAsync();
            Check(!service.IsRunning && b.Disposed && b.Stops > 0, "pause releases port and stops HID");
            await service.StopAsync(); // Idempotent pause.
            var fault = new TaskCompletionSource<WatchStatus>(TaskCreationOptions.RunContinuationsAsynchronously);
            service.StatusChanged = s => { if (s.State == WatchState.Faulted) fault.TrySetResult(s); };
            await service.StartAsync(profiles, () => throw new FormatException("foreground test failure"), () => new Device());
            await fault.Task.WaitAsync(TimeSpan.FromSeconds(5));
            await service.StopAsync();
        }
        var c = new Device(); var disposed = new WatchService();
        await disposed.StartAsync(profiles, () => note, () => c);
        await c.Applied.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await disposed.DisposeAsync();
        Check(c.Disposed && c.Stops > 0 && !disposed.IsRunning, "exit stops worker");
        try { await disposed.StartAsync(profiles, () => note, () => new Device()); throw new Exception("disposed service restarted"); }
        catch (ObjectDisposedException) { }
        Console.WriteLine("PASS: settings roundtrip/atomic save, invalid-save preservation, worker replacement, pause/resume, faults and exit.");
    }
    private sealed class Device : IManagedDevice
    {
        public TaskCompletionSource Applied { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public bool Disposed;
        public int Stops;
        private DeviceState state = new(false, true, false, Binding.Left, Binding.Right);
        public DeviceState Identify() => state;
        public DeviceState Configure(Binding left, Binding right, bool enabled)
        {
            state = new(true, enabled, true, left, right); Applied.TrySetResult(); return state;
        }
        public DeviceState KeepAlive() => state;
        public DeviceState Stop() { ++Stops; return state = state with { Managed = true, Enabled = false, LeaseActive = false }; }
        public void Dispose() => Disposed = true;
    }
}
