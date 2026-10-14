using System.Diagnostics;
using System.IO.Ports;
using System.Text;

namespace GestureControl;

public interface ILineTransport : IDisposable
{
    void Write(string text);
    string ReadLine(int timeoutMs);
}

public sealed class SerialTransport : ILineTransport
{
    private readonly SerialPort port;
    private readonly StringBuilder partial = new();
    public SerialTransport(string portName)
    {
        port = new SerialPort(portName, 115200)
        {
            DtrEnable = true, RtsEnable = false, Encoding = Encoding.ASCII,
            ReadTimeout = 100, WriteTimeout = 500, NewLine = "\n"
        };
        try { port.Open(); }
        catch { port.Dispose(); throw; }
    }
    public void Write(string text) => port.Write(text);
    public string ReadLine(int timeoutMs)
    {
        var timer = Stopwatch.StartNew();
        while (timer.ElapsedMilliseconds < timeoutMs)
        {
            port.ReadTimeout = Math.Max(1, timeoutMs - (int)timer.ElapsedMilliseconds);
            var c = (char)port.ReadChar();
            if (c == '\n') { var result = partial.ToString(); partial.Clear(); return result; }
            partial.Append(c);
            if (partial.Length > 8192) throw new InvalidDataException("Serial line exceeds 8192 characters");
        }
        throw new TimeoutException("Serial read timed out");
    }
    public void Dispose() => port.Dispose();
}

// Single owner/worker: requests and heartbeats must be serialized by the caller.
public sealed class DeviceConnection : IManagedDevice
{
    private readonly ILineTransport transport;
    private uint nextId;
    private bool ownsManagedSession;
    public Action<string>? Log { get; set; }
    public DeviceConnection(ILineTransport transport, uint? initialRequestId = null)
    {
        this.transport = transport;
        // Avoid accepting a buffered response from a previous connection's ID 1.
        nextId = initialRequestId ?? (uint)Random.Shared.Next(1, int.MaxValue);
        // Also terminates any incomplete frame from an interrupted previous connection.
        try { transport.Write("\n"); }
        catch { transport.Dispose(); throw; }
    }
    public DeviceState Identify()
    {
        // Old firmware interprets the '1' in @G1 as a legacy HID-enable command.
        // Probe with the read-only legacy info command before sending any frame.
        transport.Write("i");
        var timer = Stopwatch.StartNew();
        while (timer.ElapsedMilliseconds < 2000)
        {
            var line = transport.ReadLine(Math.Max(1, 2000 - (int)timer.ElapsedMilliseconds));
            Log?.Invoke(line);
            if (line.TrimEnd('\r') == "# Control: @G1 <id> GET/SET/KEEP/STOP/STANDALONE (LF terminated)") return Get();
        }
        throw new TimeoutException("Compatible camera firmware not found; update firmware before using this tool");
    }
    private DeviceState Request(string command)
    {
        if (++nextId == 0) ++nextId;
        transport.Write(Protocol.Frame(nextId, command));
        var timer = Stopwatch.StartNew();
        const int timeout = 1000;
        while (timer.ElapsedMilliseconds < timeout)
        {
            var line = transport.ReadLine(Math.Max(1, timeout - (int)timer.ElapsedMilliseconds));
            Log?.Invoke(line);
            var state = Protocol.Reply(line, nextId);
            if (state != null) return state;
        }
        throw new TimeoutException($"No acknowledgement for {command}");
    }
    public DeviceState Get() => Request("GET");
    public DeviceState Configure(Binding left, Binding right, bool enabled)
    {
        var command = Protocol.Set(left, right, enabled);
        // Even a lost ACK may mean the device already applied SET: STOP on disposal.
        ownsManagedSession = true;
        var state = Request(command);
        if (!state.Managed || !state.LeaseActive || state.Enabled != enabled || state.Left != left || state.Right != right)
            throw new InvalidDataException("Device applied a different profile");
        return state;
    }
    public DeviceState KeepAlive()
    {
        if (!ownsManagedSession) throw new InvalidOperationException("No managed session");
        var state = Request("KEEP");
        if (!state.Managed || !state.LeaseActive) throw new InvalidDataException("Managed session ended");
        return state;
    }
    public DeviceState Stop()
    {
        var state = Request("STOP");
        if (!state.Managed || state.Enabled || state.LeaseActive) throw new InvalidDataException("Device did not stop");
        ownsManagedSession = false;
        return state;
    }
    public DeviceState Standalone()
    {
        var state = Request("STANDALONE");
        if (state.Managed || !state.Enabled || state.Left != Binding.Left || state.Right != Binding.Right)
            throw new InvalidDataException("Device did not restore standalone mode");
        ownsManagedSession = false;
        return state;
    }
    public void Dispose()
    {
        try
        {
            if (ownsManagedSession)
            {
                try { Stop(); }
                catch (IOException) { /* Device lease is the fallback on link failure. */ }
                catch (TimeoutException) { }
                catch (InvalidDataException) { }
                catch (InvalidOperationException) { }
            }
        }
        finally { transport.Dispose(); }
    }
}
