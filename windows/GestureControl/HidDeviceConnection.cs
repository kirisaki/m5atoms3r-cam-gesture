using System.Diagnostics;

namespace GestureControl;

public interface IFeatureTransport : IDisposable
{
    void SetFeature(byte[] report);
    byte[] GetFeature();
}
public interface IControlDevice : IManagedDevice
{
    Action<string>? Log { get; set; }
    DeviceState Get();
    DeviceState Configure(Binding left, Binding right, bool enabled);
    DeviceState Standalone();
}

// One owner serializes the request/response exchange; USB callbacks never execute commands.
public sealed class HidDeviceConnection(IFeatureTransport transport, uint? initialId = null) : IControlDevice
{
    private uint nextId = initialId ?? (uint)Random.Shared.Next(1, int.MaxValue);
    private bool ownsSession, identified;
    private int maxSteps;
    public Action<string>? Log { get; set; }
    private HidReply Request(HidOperation op, uint transaction = 0, byte page = 0, byte count = 0, bool enabled = false,
        IEnumerable<KeyValuePair<string, Binding>>? entries = null)
    {
        if (++nextId == 0) ++nextId;
        transport.SetFeature(HidProtocol.Request(nextId, op, transaction, page, count, enabled, entries));
        var timer = Stopwatch.StartNew();
        while (timer.ElapsedMilliseconds < 1000)
        {
            var reply = HidProtocol.Reply(transport.GetFeature(), nextId, op);
            if (reply is not null) return reply;
            Thread.Sleep(5);
        }
        throw new TimeoutException($"HID {op} acknowledgement timed out");
    }
    private DeviceState Snapshot(HidReply first)
    {
        if (first.Page != 0) throw new InvalidDataException("HID snapshot did not start at page zero");
        var entries = first.Entries.ToList();
        for (byte page = 1; entries.Count < first.Count; ++page)
        {
            var next = Request(HidOperation.Info, page: page);
            if (next.Page != page || next.Generation != first.Generation || next.Count != first.Count ||
                next.Flags != first.Flags || next.MaxSteps != first.MaxSteps)
                throw new InvalidDataException("HID configuration changed while reading it");
            entries.AddRange(next.Entries);
        }
        GestureBindings bindings;
        try { bindings = new(entries); }
        catch (ArgumentException e) { throw new InvalidDataException("Invalid HID map", e); }
        return new((first.Flags & 1) != 0, (first.Flags & 2) != 0, (first.Flags & 4) != 0, bindings) { MaxSteps = first.MaxSteps };
    }
    public DeviceState Identify()
    {
        // The read-only Feature probe validates the vendor protocol before any command.
        if (!HidProtocol.HasSignature(transport.GetFeature())) throw new InvalidDataException("Camera HID protocol not found");
        var state = Get(); maxSteps = state.MaxSteps; identified = true;
        Log?.Invoke("HID connected; AtomS3R-CAM");
        return state;
    }
    public DeviceState Get() => Snapshot(Request(HidOperation.Info));
    public DeviceState Configure(Binding left, Binding right, bool enabled) => Configure(GestureBindings.Legacy(left, right), enabled);
    public DeviceState Configure(GestureBindings bindings, bool enabled)
    {
        if (!identified) throw new InvalidOperationException("Identify the HID camera first");
        if (maxSteps == 1 && !bindings.IsLegacy) throw new InvalidOperationException("上下・組み合わせには sequence-hid ファームウェアが必要です。");
        ownsSession = true; // Even lost COMMIT acknowledgements must be stopped on disposal.
        var token = (uint)Random.Shared.Next(1, int.MaxValue);
        var count = (byte)bindings.ActiveEntries.Count;
        Request(HidOperation.Begin, token, count: count, enabled: enabled);
        var entries = bindings.ActiveEntries.ToArray();
        for (byte page = 0; page * HidProtocol.EntriesPerPage < entries.Length; ++page)
            Request(HidOperation.Page, token, page, count, entries: entries.Skip(page * HidProtocol.EntriesPerPage).Take(HidProtocol.EntriesPerPage));
        var state = Snapshot(Request(HidOperation.Commit, token));
        if (!state.Managed || !state.LeaseActive || state.Enabled != enabled || !state.Bindings.Equals(bindings))
            throw new InvalidDataException("HID camera applied a different profile");
        return state;
    }
    public DeviceState KeepAlive()
    {
        if (!ownsSession) throw new InvalidOperationException("No managed HID session");
        var state = Snapshot(Request(HidOperation.Keep));
        if (!state.Managed || !state.LeaseActive) throw new InvalidDataException("Managed HID session ended");
        return state;
    }
    public DeviceState Stop()
    {
        var state = Snapshot(Request(HidOperation.Stop));
        if (!state.Managed || state.Enabled || state.LeaseActive) throw new InvalidDataException("HID camera did not stop");
        ownsSession = false; return state;
    }
    public DeviceState Standalone()
    {
        var state = Snapshot(Request(HidOperation.Standalone));
        if (state.Managed || !state.Enabled || !state.Bindings.Equals(GestureBindings.Legacy(Binding.Left, Binding.Right)))
            throw new InvalidDataException("HID camera did not restore standalone mode");
        ownsSession = false; return state;
    }
    public void Dispose()
    {
        try
        {
            if (ownsSession)
            {
                try { Stop(); }
                catch (Exception e) when (e is IOException or InvalidDataException or TimeoutException or InvalidOperationException) { }
            }
        }
        finally { transport.Dispose(); }
    }
}
public static class ControlDevice
{
    public static IControlDevice Open(string target = "HID", string? serial = null) => target.Equals("HID", StringComparison.OrdinalIgnoreCase)
        ? new HidDeviceConnection(WindowsHidTransport.Open(serial)) : new DeviceConnection(new SerialTransport(DesktopSettings.ValidatePort(target)));
}
