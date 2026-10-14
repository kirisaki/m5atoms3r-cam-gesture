using System.Text.Json;
using GestureControl;

internal static class ProfileTests
{
    private const string Json = """
        {"version":1,"default":{"enabled":true,"left":"Alt+Left","right":"Alt+Right"},"applications":[
          {"executable":"notepad.exe","enabled":true,"left":"Left","right":"Right"},
          {"executable":"mspaint.exe","enabled":false,"left":"None","right":"None"}]}
        """;
    private static void Check(bool condition, string name) { if (!condition) throw new Exception(name); }
    public static void Run()
    {
        var settings = ProfileSettings.Parse(Json);
        Check(settings.Resolve(@"C:\Program Files\WindowsApps\Notepad.EXE").Left == new Binding(0, 80), "case-insensitive executable name");
        Check(settings.Resolve(@"C:\Browser\browser.exe") == settings.Default, "default for other known apps");
        Check(!settings.Resolve("mspaint.exe").Enabled, "per-app disable");
        foreach (var json in new[] { "null", "{}", Json.Replace("\"version\":1", "\"version\":2"),
            Json.Replace("\"enabled\":true,", ""), Json.Replace("\"left\":", "\"lft\":"),
            Json.Replace("\"Left\"", "null"), Json.Replace("\"Left\"", "\"Ctrl+None\""),
            Json.Replace("mspaint.exe", "NOTEPAD.EXE"), Json.Replace("notepad.exe", "*.exe"),
            Json.Replace("notepad.exe", "C:/notepad.exe"), Json.Replace("notepad.exe", " notepad.exe"),
            Json.Replace("notepad.exe", "notepad"), Json.Replace("\"applications\":[", "\"applications\":[null,") })
        {
            try { ProfileSettings.Parse(json); }
            catch (Exception e) when (e is JsonException or InvalidDataException) { continue; }
            throw new Exception($"Invalid settings accepted: {json}");
        }

        long now = 0;
        var note = new ForegroundApp(1, 11, @"C:\Apps\notepad.exe");
        var browser = new ForegroundApp(2, 22, @"C:\Apps\browser.exe");
        ForegroundApp? current = note;
        var first = new FakeDevice();
        var second = new FakeDevice();
        var devices = new Queue<FakeDevice>([first, second]);
        using (var watcher = new ProfileWatcher(settings, () => current, () => devices.Dequeue(), () => now))
        {
            watcher.Tick();
            Check(first.Commands.SequenceEqual(new[] { "IDENTIFY", "STOP" }), "stop before stable foreground");
            now = 99; watcher.Tick(); Check(first.Profiles.Count == 0, "focus debounce");
            now = 100; watcher.Tick(); Check(first.Profiles.Single().Left == new Binding(0, 80), "notepad profile");
            now = 599; watcher.Tick(); Check(!first.Commands.Contains("KEEP"), "heartbeat interval");
            now = 600; watcher.Tick(); Check(first.Commands[^1] == "KEEP", "heartbeat");
            current = browser; now = 601; watcher.Tick(); Check(first.Commands[^1] == "STOP", "stop old app immediately");
            now = 701; watcher.Tick(); Check(first.Profiles[^1].Left == Binding.Left, "browser defaults");
            current = new(3, 33, @"C:\Windows\mspaint.exe"); now = 702; watcher.Tick();
            now = 802; watcher.Tick(); Check(!first.Profiles[^1].Enabled, "disabled app");
            current = null; now = 803; watcher.Tick(); Check(first.Commands[^1] == "STOP", "unknown foreground stops");
            var count = first.Commands.Count;
            now = 1500; watcher.Tick(); Check(first.Commands.Count == count, "unknown never falls back to enabled default");
            current = note; now = 1501; watcher.Tick(); now = 1601; watcher.Tick();
            first.FailHeartbeat = true;
            now = 2101; watcher.Tick(); Check(first.Disposed, "disconnect closes connection");
            current = browser; now = 4100; watcher.Tick(); Check(devices.Count == 1, "reconnect backoff");
            // Change focus DURING handshake: only the latest foreground may be applied.
            second.OnIdentify = () => current = note;
            now = 4101; watcher.Tick();
            now = 4201; watcher.Tick(); Check(second.Profiles.Single().Left == new Binding(0, 80), "fresh foreground after reconnect");
            // Change focus during SET: STOP and start selection again, no stale applied state.
            current = browser; now = 4202; watcher.Tick();
            second.OnConfigure = () => current = note;
            now = 4302; watcher.Tick(); Check(second.Commands[^1] == "STOP", "focus changed during ACK");
            second.OnConfigure = null;
            now = 4303; watcher.Tick(); now = 4403; watcher.Tick();
            Check(second.Profiles[^1].Left == new Binding(0, 80), "recover focus change");
            // A live but altered device mapping must never receive continuing heartbeats.
            second.MismatchHeartbeat = true;
            now = 4903; watcher.Tick(); Check(second.Disposed, "mismatching heartbeat stops and reconnects");
        }
        Check(second.Commands[^1] == "STOP", "shutdown stopped device");

        // Failed identification must not send framed commands to old firmware.
        var old = new FakeDevice { FailIdentify = true };
        using (var watcher = new ProfileWatcher(settings, () => note, () => old, () => 0)) watcher.Tick();
        Check(old.Disposed && old.Commands.SequenceEqual(new[] { "IDENTIFY" }), "no STOP to incompatible firmware");
        // Failed SET may have applied: release it even though there was no ACK.
        var lost = new FakeDevice { FailConfigure = true };
        now = 0;
        using (var watcher = new ProfileWatcher(settings, () => note, () => lost, () => now))
        {
            watcher.Tick(); now = 100; watcher.Tick();
            Check(lost.Disposed && lost.Commands[^1] == "STOP", "failed SET cleanup");
        }
        var shutdown = new FakeDevice(); now = 0;
        using (var watcher = new ProfileWatcher(settings, () => note, () => shutdown, () => now))
        { watcher.Tick(); now = 100; watcher.Tick(); }
        Check(shutdown.Disposed && shutdown.Commands[^1] == "STOP", "normal watch exit");
        Console.WriteLine("PASS: profile validation, focus changes, defaults/disable, heartbeat, disconnect/reconnect, stale focus, lost ACK and shutdown.");
    }
    private sealed class FakeDevice : IManagedDevice
    {
        public List<string> Commands { get; } = [];
        public List<DeviceState> Profiles { get; } = [];
        public Action? OnIdentify, OnConfigure;
        public bool FailIdentify, FailHeartbeat, FailConfigure, MismatchHeartbeat, Disposed;
        private DeviceState state = new(false, true, false, Binding.Left, Binding.Right);
        public DeviceState Identify()
        {
            Commands.Add("IDENTIFY"); OnIdentify?.Invoke();
            if (FailIdentify) throw new TimeoutException("old firmware");
            return state;
        }
        public DeviceState Configure(GestureBindings bindings, bool enabled)
        {
            Commands.Add("SET"); state = new(true, enabled, true, bindings); Profiles.Add(state);
            OnConfigure?.Invoke();
            if (FailConfigure) throw new TimeoutException("lost ACK");
            return state;
        }
        public DeviceState KeepAlive()
        {
            Commands.Add("KEEP");
            if (FailHeartbeat) throw new IOException("USB disconnected");
            return MismatchHeartbeat ? state with { Enabled = !state.Enabled } : state;
        }
        public DeviceState Stop() { Commands.Add("STOP"); return state = state with { Managed = true, Enabled = false, LeaseActive = false }; }
        public void Dispose() => Disposed = true;
    }
}
