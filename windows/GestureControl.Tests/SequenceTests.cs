using System.Text.Json;
using GestureControl;

internal static class SequenceTests
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}");
    }
    public static void Run()
    {
        const string json = """
            {"version":2,"default":{"enabled":true,"bindings":{"L":"Alt+Left","R":"Alt+Right","U":"None","UR":"None","RD":"Ctrl+Z","RDL":"Home"}},
             "applications":[{"executable":"notepad.exe","enabled":true,"bindings":{"U":"Up","D":"Down","RDL":"End"}}]}
            """;
        var settings = ProfileSettings.Parse(json);
        Check(settings.Default.Bindings["RD"] == Binding.Parse("Ctrl+Z"), "two-step setting");
        Check(settings.Resolve("NOTEPAD.EXE").Bindings["RDL"] == Binding.Parse("End"), "three-step app override");
        Check(settings.Resolve("NOTEPAD.EXE").Left == default, "no implicit fallback to default path");
        Check(ProfileSettings.Parse(settings.ToJson()).Default == settings.Default, "v2 round trip");
        Check(ProfileSettings.Parse(settings.ToJson()).Default.Bindings.Entries.ContainsKey("UR"), "preserve disabled combo row");
        Check(!Protocol.Set(settings.Default.Bindings, true).Contains(" UR "), "omit disabled combo from wire");
        const string old = """{"version":1,"default":{"enabled":true,"left":"Left","right":"Right"},"applications":[]}""";
        var migrated = ProfileSettings.Parse(ProfileSettings.Parse(old).ToJson());
        Check(migrated.Default.Left == Binding.Parse("Left") && migrated.Default.Bindings["U"] == default, "v1 migration preserves bindings");
        foreach (var bad in new[] { "", "RR", "RDLU", "rd", "R>D", "Q" })
        {
            Check(!GestureBindings.ValidPath(bad), "reject path");
            Throws<InvalidDataException>(() => ProfileSettings.Parse(json.Replace("\"RD\":", $"\"{bad}\":")));
        }
        Throws<InvalidDataException>(() => ProfileSettings.Parse(json.Replace("\"U\":\"None\"", "\"RD\":\"None\"")));
        Throws<InvalidDataException>(() => ProfileSettings.Parse(json.Replace("\"Home\"", "null")));
        var map = new GestureBindings(new Dictionary<string, Binding> { ["RD"] = new(1, 29), ["RDL"] = new(0, 74) });
        Check(Protocol.Frame(2, Protocol.Set(map, true), 2) == "@G2 2 SET 1 2 RD 1 29 RDL 0 74\n", "G2 wire format");
        Check(Protocol.Reply("@G2 2 OK ATOMS3R_CAM MANAGED 1 1 3 2 RDL 0 74 RD 1 29", 2, 2)!.Bindings.Equals(map), "map order independent readback");
        foreach (var bad in new[] {
            "@G2 2 OK ATOMS3R_CAM MANAGED 1 1 3 2 RD 1 29 RD 0 74",
            "@G2 2 OK ATOMS3R_CAM MANAGED 1 1 1 1 RDL 0 74",
            "@G2 2 OK ATOMS3R_CAM MANAGED 1 0 3 0",
            "@G2 2 OK ATOMS3R_CAM MANAGED 1 1 3 1 RR 0 74",
            "@G2 2 OK ATOMS3R_CAM MANAGED 1 1 3 1 U 1 0",
            "@G2 2 OK ATOMS3R_CAM MANAGED 1 1 3 0 extra",
            "@G2 2 OK ATOMS3R_CAM MANAGED 1 1 3 53" })
            Throws<InvalidDataException>(() => Protocol.Reply(bad, 2, 2));
        Check(Protocol.Reply("@G1 2 OK ATOMS3R_CAM MANAGED 1 1 4 80 4 79", 2, 2) is null, "old protocol ACK isolation");
        Throws<DeviceException>(() => Protocol.Reply("@G1 0 ERR FRAME", 2, 2));
        var all = new List<KeyValuePair<string, Binding>>();
        void AddPaths(string path)
        {
            if (path.Length > 0) all.Add(new(path, new(255, 115)));
            if (path.Length == 3) return;
            foreach (var direction in "LRUD") if (path.Length == 0 || path[^1] != direction) AddPaths(path + direction);
        }
        AddPaths("");
        Check(GestureBindings.Paths.Count == 52 && GestureBindings.Paths.ToHashSet().SetEquals(all.Select(e => e.Key)), "path catalogue");
        Check(all.Count == 52 && Protocol.Frame(uint.MaxValue, Protocol.Set(new(all), true), 2).Length < 1024, "full map fits frame");
        var wire = new FakeTransport();
        using (var device = new DeviceConnection(wire, 0))
        {
            wire.Lines.Enqueue("# Control: @G2 <id> GET/SET/KEEP/STOP/STANDALONE (LF terminated)");
            wire.Lines.Enqueue("@G2 1 OK ATOMS3R_CAM STANDALONE 0 0 3 2 L 4 80 R 4 79");
            Check(device.Identify().MaxSteps == 3, "G2 handshake");
            wire.Lines.Enqueue("@G2 2 OK ATOMS3R_CAM MANAGED 0 1 3 2 RD 1 29 RDL 0 74");
            device.Configure(map, false);
            wire.Lines.Enqueue("@G2 3 OK ATOMS3R_CAM MANAGED 0 1 3 2 RDL 0 74 RD 1 29");
            device.KeepAlive();
            // Different full path with identical single-direction values must fail verification.
            wire.Lines.Enqueue("@G2 4 OK ATOMS3R_CAM MANAGED 0 1 3 1 RDU 0 74");
            Throws<InvalidDataException>(() => device.Configure(map, false));
            wire.Lines.Enqueue("@G2 5 OK ATOMS3R_CAM MANAGED 0 0 3 1 RDU 0 74");
        }
        Check(wire.Writes[^1] == "@G2 5 STOP\n", "G2 failed ACK cleanup");
        wire = new FakeTransport();
        using (var device = new DeviceConnection(wire, 0))
            Throws<InvalidOperationException>(() => device.Configure(map, true));
        Check(wire.Writes.SequenceEqual(new[] { "\n" }), "unsupported map rejected before any SET");
        long now = 0;
        var note = new ForegroundApp(1, 1, "notepad.exe");
        ForegroundApp current = note;
        var fake = new SequenceDevice();
        using (var watcher = new ProfileWatcher(settings, () => current, () => fake, () => now))
        {
            watcher.Tick(); now = 100; watcher.Tick();
            Check(fake.State.Bindings["RDL"] == Binding.Parse("End"), "watcher sends full map");
            current = new(2, 2, "browser.exe"); now = 101; watcher.Tick();
            Check(!fake.State.Enabled, "focus change stops map");
            now = 201; watcher.Tick();
            Check(fake.State.Bindings["RDL"] == Binding.Parse("Home"), "full map switching");
            fake.State = fake.State with { Bindings = GestureBindings.Legacy(fake.State.Left, fake.State.Right) };
            now = 701; watcher.Tick();
            Check(fake.Disposed, "heartbeat checks combo bindings too");
        }
        Console.WriteLine("PASS: 52 gesture paths, v1 migration, v2 validation, atomic map readback, capability negotiation and full-map focus switching.");
    }
    private sealed class SequenceDevice : IManagedDevice
    {
        public DeviceState State = new(false, false, false, new GestureBindings([]));
        public bool Disposed;
        public DeviceState Identify() => State;
        public DeviceState Configure(GestureBindings bindings, bool enabled) => State = new(true, enabled, true, bindings);
        public DeviceState KeepAlive() => State;
        public DeviceState Stop() => State = State with { Managed = true, Enabled = false, LeaseActive = false };
        public void Dispose() => Disposed = true;
    }
}
