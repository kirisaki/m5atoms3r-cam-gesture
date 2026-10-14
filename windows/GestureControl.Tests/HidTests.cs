using System.Buffers.Binary;
using GestureControl;

internal static class HidTests
{
    private static void Check(bool condition, string name) { if (!condition) throw new Exception(name); }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}");
    }
    private static byte[] Reply(byte[] request, GestureBindings map, byte flags = 5, uint generation = 9)
    {
        var report = new byte[64]; request.AsSpan(0, 11).CopyTo(report);
        report[6] = 0;
        report[11] = request[5] == (byte)HidOperation.Info ? request[11] : (byte)0;
        report[12] = (byte)map.ActiveEntries.Count; report[13] = flags; report[14] = 3;
        BinaryPrimitives.WriteUInt32LittleEndian(report.AsSpan(15), generation);
        var index = 0;
        foreach (var pair in map.ActiveEntries.Skip(report[11] * 15).Take(15))
        {
            var offset = 19 + 3 * index++;
            report[offset] = HidProtocol.PathCode(pair.Key); report[offset + 1] = pair.Value.Modifiers; report[offset + 2] = pair.Value.Usage;
        }
        return report;
    }
    public static void Run()
    {
        foreach (var path in GestureBindings.Paths) Check(HidProtocol.PathText(HidProtocol.PathCode(path)) == path, "path code roundtrip");
        foreach (var code in new byte[] { 0, 5, 6, 12, 25, 124, 125, 255 }) Throws<InvalidDataException>(() => HidProtocol.PathText(code));
        var info = HidProtocol.Request(7, HidOperation.Info);
        Check(info.AsSpan(0, 11).SequenceEqual(new byte[] { 6, 65, 71, 72, 49, 1, 0, 7, 0, 0, 0 }), "wire header fixture");
        var golden = new byte[64];
        new byte[] { 6, 65, 71, 72, 49, 1, 0, 7, 0, 0, 0, 0, 2, 2, 3, 0, 0, 0, 0, 1, 4, 80, 2, 4, 79 }.CopyTo(golden, 0);
        Check(HidProtocol.Reply(golden, 7, HidOperation.Info)!.Entries[1].Value == Binding.Right, "firmware golden snapshot");
        Check(HidProtocol.Reply(golden, 8, HidOperation.Info) is null, "stale HID reply");
        Throws<InvalidDataException>(() => HidProtocol.Reply(golden, 7, HidOperation.Keep));
        foreach (var (offset, value) in new (int, byte)[] { (0, 0), (1, 0), (11, 4), (12, 53), (13, 4), (14, 2), (19, 6), (21, 0), (63, 1) })
        {
            var bad = (byte[])golden.Clone(); bad[offset] = value;
            Throws<InvalidDataException>(() => HidProtocol.Reply(bad, 7, HidOperation.Info));
        }
        var error = (byte[])golden.Clone(); error[6] = 4;
        Throws<DeviceException>(() => HidProtocol.Reply(error, 7, HidOperation.Info));
        var all = new GestureBindings(GestureBindings.Paths.Select(p => new KeyValuePair<string, Binding>(p, new(255, 115))));
        var empty = new GestureBindings([]);
        var current = empty;
        byte currentFlags = 5;
        var wire = new FeatureFake
        {
            Respond = request =>
            {
                if (request[5] == (byte)HidOperation.Commit) current = all;
                if (request[5] == (byte)HidOperation.Stop) currentFlags = 1;
                return Reply(request, current, currentFlags);
            },
            StaleOnce = true
        };
        using (var device = new HidDeviceConnection(wire, 0))
        {
            device.Identify();
            Check(device.Configure(all, false).Bindings.Equals(all), "atomic all-map configuration");
            Check(device.KeepAlive().Bindings.Equals(all), "HID heartbeat reads entire map");
            device.Stop();
        }
        var pages = wire.Writes.Where(r => r[5] == (byte)HidOperation.Page).ToArray();
        Check(pages.Length == 4 && pages.Select(r => r[11]).SequenceEqual(new byte[] { 0, 1, 2, 3 }), "ordered upload pages");
        var sent = new List<string>();
        foreach (var page in pages)
        {
            Check(page[12] == 52 && page[13] == 0, "page metadata");
            for (var offset = 19; offset < 64 && page[offset] != 0; offset += 3)
            { sent.Add(HidProtocol.PathText(page[offset])); Check(page[offset + 1] == 255 && page[offset + 2] == 115, "page contents"); }
        }
        Check(sent.ToHashSet().SetEquals(GestureBindings.Paths) && wire.Disposed, "all bindings delivered once and closed");
        // Reading a changing snapshot must fail instead of accepting a mixed map.
        wire = new FeatureFake { Respond = request => Reply(request, all, generation: request[11] == 1 ? 10u : 9u) };
        using (var device = new HidDeviceConnection(wire, 0)) Throws<InvalidDataException>(() => device.Get());
        // Duplicate paths across pages are rejected as well as malformed individual entries.
        wire = new FeatureFake { Respond = request =>
        {
            var reply = Reply(request, all);
            if (request[11] == 1) reply[19] = HidProtocol.PathCode(all.ActiveEntries.First().Key);
            return reply;
        } };
        using (var device = new HidDeviceConnection(wire, 0)) Throws<InvalidDataException>(() => device.Get());
        // Lost commit ACK: stop even though the host cannot know whether it applied.
        wire = new FeatureFake { Respond = request =>
        {
            var reply = Reply(request, empty, 1);
            if (request[5] == (byte)HidOperation.Commit) BinaryPrimitives.WriteUInt32LittleEndian(reply.AsSpan(7), 0);
            return reply;
        } };
        using (var device = new HidDeviceConnection(wire, 0))
        { device.Identify(); Throws<TimeoutException>(() => device.Configure(empty, false)); }
        Check(wire.Writes[^1][5] == (byte)HidOperation.Stop, "uncertain commit cleanup");
        // Read-only probe rejects a different HID protocol before any SET_FEATURE.
        wire = new FeatureFake { Probe = new byte[64] };
        using (var device = new HidDeviceConnection(wire, 0)) Throws<InvalidDataException>(() => device.Identify());
        Check(wire.Writes.Count == 0, "no commands to incompatible HID");
        var legacy = DesktopSettings.Parse("""{"version":1,"port":"COM10","profiles":{"version":1,"default":{"enabled":true,"left":"Left","right":"Right"},"applications":[]}}""");
        Check(legacy.DeviceSerial is null && legacy.Profiles.Default.Left == Binding.Parse("Left"), "legacy settings migrate to HID auto detection");
        var settings = legacy with { DeviceSerial = "70041DD57480" };
        Check(DesktopSettings.Parse(settings.ToJson()).DeviceSerial == settings.DeviceSerial, "selected HID identity persists");
        Throws<InvalidDataException>(() => (settings with { DeviceSerial = "bad\nidentity" }).ToJson());
        Console.WriteLine("PASS: HID wire fixtures, 52-binding transaction/readback, stale replies, changing snapshots, lost ACK cleanup and settings migration.");
    }
    private sealed class FeatureFake : IFeatureTransport
    {
        public List<byte[]> Writes { get; } = [];
        public Func<byte[], byte[]> Respond = request => Reply(request, new([]), 1);
        public byte[] Probe = HidProtocol.Request(1, HidOperation.Info);
        private byte[]? current, previous;
        private bool stale;
        public bool StaleOnce, Disposed;
        public void SetFeature(byte[] report) { Writes.Add(report); previous = current; current = Respond(report); stale = StaleOnce && previous is not null; }
        public byte[] GetFeature() { if (stale) { stale = false; return previous!; } return current ?? Probe; }
        public void Dispose() => Disposed = true;
    }
}
