using System.Diagnostics;
using System.IO.Ports;
using System.Text.Json;
using GestureControl;

try
{
    if (args.Length > 0 && args[0] is "watch" or "foreground") return WatchCommands.Run(args);
    if (args.Length == 1 && args[0] == "ports")
    {
        foreach (var name in SerialPort.GetPortNames().Order()) Console.WriteLine(name);
        return 0;
    }
    if (args.Length < 2 || args[0] is not ("info" or "run" or "stop" or "standalone" or "verify"))
    {
        Console.WriteLine("""
            GestureControl (.NET 10)
              ports
              info COM10
              run COM10 [--left Alt+Left] [--right Alt+Right] [--disabled] [--seconds 30]
              stop COM10
              standalone COM10
              verify COM10
              foreground [--seconds 30]
              watch COM10 --config profiles.json [--seconds 60]
            run keeps the profile active until Ctrl+C or --seconds, then stops HID.
            None disables a direction. verify checks configuration/lease without sending keys and leaves HID stopped.
            standalone restores Alt+Left / Alt+Right without requiring this app.
            """);
        return args.Length == 0 ? 0 : 2;
    }
    var left = Binding.Left;
    var right = Binding.Right;
    var enabled = true;
    var seconds = 0;
    for (var i = 2; i < args.Length; ++i)
    {
        if (args[0] != "run") throw new ArgumentException("Options are only valid with run");
        switch (args[i])
        {
            case "--left" when i + 1 < args.Length: left = Binding.Parse(args[++i]); break;
            case "--right" when i + 1 < args.Length: right = Binding.Parse(args[++i]); break;
            case "--disabled": enabled = false; break;
            case "--seconds" when i + 1 < args.Length && int.TryParse(args[++i], out seconds) && seconds > 0: break;
            default: throw new ArgumentException("Unknown option or invalid value");
        }
    }
    using var device = new DeviceConnection(new SerialTransport(args[1]));
    device.Log = line => { if (!line.StartsWith("@G1 ") && !line.StartsWith("@G2 ")) Console.Error.WriteLine(line); };
    var initial = device.Identify();
    Print(initial); // Validate firmware/model before sending control frames.
    switch (args[0])
    {
        case "info": break;
        case "stop": Print(device.Stop()); break;
        case "standalone": Print(device.Standalone()); break;
        case "run":
            using (var cancel = new CancellationTokenSource())
            {
                Console.CancelKeyPress += (_, e) => { e.Cancel = true; cancel.Cancel(); };
                Print(device.Configure(left, right, enabled));
                Console.WriteLine("Profile active. Ctrl+C stops HID; standalone restores default operation.");
                var timer = Stopwatch.StartNew();
                while (!cancel.Token.WaitHandle.WaitOne(500) && (seconds == 0 || timer.Elapsed.TotalSeconds < seconds))
                    device.KeepAlive();
                Print(device.Stop());
            }
            break;
        case "verify":
            if (initial.MaxSteps == 3)
            {
                var fullMap = new GestureBindings(GestureBindings.Paths.Select(path => new KeyValuePair<string, Binding>(path, new(255, 115))));
                device.Configure(fullMap, false);
                Require(device.Get().Bindings.Equals(fullMap), "52-binding map readback (USB receive capacity)");
                Console.WriteLine("PASS: all 52 bindings transferred atomically with HID disabled.");
                var map = new GestureBindings(new Dictionary<string, Binding>
                {
                    ["U"] = Binding.Parse("Up"), ["D"] = Binding.Parse("Down"),
                    ["RD"] = Binding.Parse("Ctrl+Z"), ["RDL"] = Binding.Parse("Home")
                });
                Print(device.Configure(map, false));
                Require(device.Get().Bindings.Equals(map), "sequence map readback");
                Require(device.KeepAlive().Bindings.Equals(map), "sequence heartbeat readback");
            }
            Print(device.Configure(Binding.Parse("Ctrl+PageUp"), Binding.Parse("Ctrl+PageDown"), false));
            for (var i = 0; i < 6; ++i) { Thread.Sleep(500); device.KeepAlive(); }
            var state = device.Get();
            Require(!state.Enabled && state.LeaseActive && state.Left == Binding.Parse("Ctrl+PageUp"), "disabled profile readback");
            // Enabled, but both directions unbound: exercise expiry with no key press.
            Print(device.Configure(default, default, true));
            Thread.Sleep(2300);
            state = device.Get();
            Require(state.Managed && !state.Enabled && !state.LeaseActive, "heartbeat expiry");
            try { device.KeepAlive(); throw new InvalidDataException("Expired KEEP unexpectedly accepted"); }
            catch (DeviceException e) when (e.Code == "EXPIRED") { }
            Print(device.Configure(default, default, false));
            Print(device.Stop());
            Console.WriteLine("PASS: mapping readback, heartbeat, expiry, no revival by KEEP, reconfiguration and STOP. No keys sent.");
            break;
    }
    return 0;
}
catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException or TimeoutException or ArgumentException or InvalidOperationException or JsonException or PlatformNotSupportedException)
{
    Console.Error.WriteLine($"ERROR: {e.Message}");
    return 1;
}

static void Print(DeviceState state) => Console.WriteLine(JsonSerializer.Serialize(state));
static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidDataException($"Verification failed: {message}");
}
