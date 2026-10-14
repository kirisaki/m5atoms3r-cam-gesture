using System.Diagnostics;
using GestureControl;

internal static class WatchCommands
{
    public static int Run(string[] args)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("Foreground monitoring requires Windows");
        var watch = args[0] == "watch";
        if (watch && (args.Length < 2 || !args[1].StartsWith("COM", StringComparison.OrdinalIgnoreCase)))
            throw new ArgumentException("watch requires an explicit Windows COM port");
        string? config = null;
        var seconds = 0;
        for (var i = watch ? 2 : 1; i < args.Length; ++i)
        {
            switch (args[i])
            {
                case "--config" when watch && i + 1 < args.Length && config is null: config = args[++i]; break;
                case "--seconds" when i + 1 < args.Length && int.TryParse(args[++i], out seconds) && seconds > 0: break;
                default: throw new ArgumentException("Unknown option or invalid value");
            }
        }
        if (watch && config is null) throw new ArgumentException("watch requires --config PATH");
        // Parse the entire file before opening a port; invalid settings change no device state.
        var settings = config is null ? null : ProfileSettings.Load(config);
        using var cancel = new CancellationTokenSource();
        ConsoleCancelEventHandler handler = (_, e) => { e.Cancel = true; cancel.Cancel(); };
        Console.CancelKeyPress += handler;
        try
        {
            using var watcher = settings is null ? null : new ProfileWatcher(settings, WindowsForeground.Current, () =>
            {
                var device = new DeviceConnection(new SerialTransport(args[1]));
                device.Log = line =>
                {
                    if (line.StartsWith("# HID LEFT ") || line.StartsWith("# HID RIGHT ")) Log(line);
                };
                return device;
            }) { Log = Log };
            Log(watch ? $"WATCH {args[1]}; Ctrl+C stops HID. Settings loaded from {config}" : "FOREGROUND monitor (no USB access)");
            var timer = Stopwatch.StartNew();
            ForegroundApp? previous = null;
            var first = true;
            do
            {
                if (watcher is not null) watcher.Tick();
                else
                {
                    var current = WindowsForeground.Current();
                    if (first || current != previous)
                        Log(current is null ? "FOREGROUND unavailable" : $"FOREGROUND {current.Executable} pid={current.ProcessId}");
                    previous = current; first = false;
                }
            }
            while (!cancel.Token.WaitHandle.WaitOne(ProfileWatcher.PollMs) && (seconds == 0 || timer.Elapsed.TotalSeconds < seconds));
            return 0;
        }
        finally { Console.CancelKeyPress -= handler; }
    }
    private static void Log(string message) => Console.WriteLine($"{DateTime.Now:HH:mm:ss.fff} {message}");
}
