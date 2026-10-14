using GestureControl;

static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
}
static void Throws<T>(Action action) where T : Exception
{
    try { action(); } catch (T) { return; }
    throw new Exception($"Expected {typeof(T).Name}");
}

Check(Binding.Parse("Alt+Left") == new Binding(4, 80), "Alt+Left");
Check(Binding.Parse("Ctrl+Shift+Tab") == new Binding(3, 43), "Ctrl+Shift+Tab");
Check(Binding.Parse("ralt+f24") == new Binding(64, 115), "F24");
Check(Binding.Parse("F13") == new Binding(0, 104), "F13");
Check(Binding.Parse("None") == default, "disabled binding");
foreach (var bad in new[] { "Ctrl+None", "Ctrl+", "u\n", "F25", "Ctrl+Ctrl+A", "Alt+Unknown", "" })
    Throws<ArgumentException>(() => Binding.Parse(bad));
Check(Protocol.Frame(1, Protocol.Set(Binding.Left, Binding.Right, true)) == "@G1 1 SET 4 80 4 79 1\n", "wire format");
Throws<ArgumentException>(() => Protocol.Frame(0, "GET"));
Throws<ArgumentException>(() => Protocol.Frame(1, "GET\nu"));
Throws<ArgumentException>(() => Protocol.Set(new(4, 0), default, true));
Check(Protocol.Reply("# HID ready=1", 1) is null, "diagnostic isolation");
Check(Protocol.Reply("ALT_LEFT", 1) is null, "gesture isolation");
Check(Protocol.Reply("@G1 9 OK ATOMS3R_CAM MANAGED 1 1 4 80 4 79", 1) is null, "stale ACK isolation");
var state = Protocol.Reply("@G1 1 OK ATOMS3R_CAM MANAGED 1 1 4 80 4 79\r", 1);
Check(state is { Managed: true, Enabled: true, LeaseActive: true, Left: { Usage: 80 } }, "readback");
Throws<DeviceException>(() => Protocol.Reply("@G1 1 ERR EXPIRED", 1));
Throws<DeviceException>(() => Protocol.Reply("@G1 0 ERR FRAME", 1));
foreach (var bad in new[] {
    "@G1 1 OK OTHER MANAGED 1 1 4 80 4 79", "@G1 1 OK ATOMS3R_CAM UNKNOWN 1 1 4 80 4 79",
    "@G1 1 OK ATOMS3R_CAM MANAGED 1 0 4 80 4 79", "@G1 1 OK ATOMS3R_CAM MANAGED 1 1 4 0 4 79",
    "@G1 1 OK ATOMS3R_CAM MANAGED 2 1 4 80 4 79", "@G1 1 OK ATOMS3R_CAM STANDALONE 1 1 4 80 4 79",
    "@G1 1 OK ATOMS3R_CAM MANAGED 1 1 256 80 4 79" })
    Throws<InvalidDataException>(() => Protocol.Reply(bad, 1));

var wire = new FakeTransport();
using (var device = new DeviceConnection(wire, initialRequestId: 0))
{
    wire.Lines.Enqueue("# READY");
    wire.Lines.Enqueue("@G1 999 OK ATOMS3R_CAM STANDALONE 1 0 4 80 4 79");
    wire.Lines.Enqueue("@G1 1 OK ATOMS3R_CAM STANDALONE 1 0 4 80 4 79");
    Check(!device.Get().Managed, "probe");
    wire.Lines.Enqueue("@G1 2 OK ATOMS3R_CAM MANAGED 0 1 1 75 1 78");
    device.Configure(Binding.Parse("Ctrl+PageUp"), Binding.Parse("Ctrl+PageDown"), false);
    wire.Lines.Enqueue("@G1 3 OK ATOMS3R_CAM MANAGED 0 1 1 75 1 78");
    device.KeepAlive();
    wire.Lines.Enqueue("@G1 4 OK ATOMS3R_CAM MANAGED 0 0 1 75 1 78");
}
Check(wire.Disposed && wire.Writes.SequenceEqual(new[] { "\n", "@G1 1 GET\n", "@G1 2 SET 1 75 1 78 0\n", "@G1 3 KEEP\n", "@G1 4 STOP\n" }), "cleanup STOP");
// A SET whose ACK was lost must still be stopped on disposal.
wire = new FakeTransport();
using (var device = new DeviceConnection(wire, initialRequestId: 0))
{
    Throws<TimeoutException>(() => device.Configure(Binding.Left, Binding.Right, true));
    wire.Lines.Enqueue("@G1 2 OK ATOMS3R_CAM MANAGED 0 0 4 80 4 79");
}
Check(wire.Writes[^1] == "@G1 2 STOP\n", "uncertain SET cleanup");
// A mismatching ACK is a failure, not a successfully applied profile.
wire = new FakeTransport();
using (var device = new DeviceConnection(wire, initialRequestId: 0))
{
    wire.Lines.Enqueue("@G1 1 OK ATOMS3R_CAM MANAGED 1 1 4 80 4 79");
    Throws<InvalidDataException>(() => device.Configure(default, default, false));
}
Check(wire.Disposed, "disposal after failed STOP");
Console.WriteLine("PASS: bindings, framing, ACK validation, stale messages, lost ACK cleanup and profile consistency.");

wire = new FakeTransport();
using (var device = new DeviceConnection(wire, initialRequestId: 0))
{
    wire.Lines.Enqueue("# AtomS3R-CAM old firmware");
    Throws<TimeoutException>(() => device.Identify());
}
Check(wire.Writes.SequenceEqual(new[] { "\n", "i" }), "old firmware must never receive @G1");
wire = new FakeTransport();
using (var device = new DeviceConnection(wire, initialRequestId: 0))
{
    wire.Lines.Enqueue("# Control: @G1 <id> GET/SET/KEEP/STOP/STANDALONE (LF terminated)");
    wire.Lines.Enqueue("@G1 1 OK ATOMS3R_CAM STANDALONE 1 0 4 80 4 79");
    Check(!device.Identify().Managed, "firmware capability handshake");
}
Console.WriteLine("PASS: firmware compatibility probe.");

ProfileTests.Run();
await DesktopTests.RunAsync();

sealed class FakeTransport : ILineTransport
{
    public List<string> Writes { get; } = [];
    public Queue<string> Lines { get; } = new();
    public bool Disposed { get; private set; }
    public void Write(string text) => Writes.Add(text);
    public string ReadLine(int timeoutMs) => Lines.TryDequeue(out var line) ? line : throw new TimeoutException();
    public void Dispose() => Disposed = true;
}
