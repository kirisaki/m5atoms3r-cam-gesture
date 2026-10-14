namespace GestureControl;

public enum WatchState { WaitingForFocus, ForegroundUnavailable, Applied, Disconnected, Stopped, Faulted }
public sealed record WatchStatus(WatchState State, string? Executable = null, ShortcutProfile? Profile = null, string? Error = null);

public interface IManagedDevice : IDisposable
{
    DeviceState Identify();
    DeviceState Configure(Binding left, Binding right, bool enabled);
    DeviceState KeepAlive();
    DeviceState Stop();
}

// One worker owns both foreground selection and serial IO. Never keep an old
// profile alive on another worker while foreground lookup/reconnection stalls.
public sealed class ProfileWatcher : IDisposable
{
    public const int PollMs = 100, StableMs = 100, HeartbeatMs = 500, ReconnectMs = 2000;
    private readonly ProfileSettings settings;
    private readonly Func<ForegroundApp?> foreground;
    private readonly Func<IManagedDevice> connect;
    private readonly Func<long> clock;
    private IManagedDevice? device;
    private ForegroundApp? candidate;
    private ShortcutProfile? applied;
    private long candidateSince, heartbeatAt, retryAt;
    private bool observed, disposed, identified;
    public Action<string>? Log { get; set; }
    public Action<WatchStatus>? StatusChanged { get; set; }

    public ProfileWatcher(ProfileSettings settings, Func<ForegroundApp?> foreground,
        Func<IManagedDevice> connect, Func<long>? clock = null)
    {
        this.settings = settings; this.foreground = foreground; this.connect = connect;
        this.clock = clock ?? (() => Environment.TickCount64);
    }
    public void Tick()
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        try
        {
            if (device is null)
            {
                if (clock() < retryAt) return;
                device = connect();
                device.Identify();
                identified = true;
                device.Stop(); // Stop standalone/previous profile before selecting the CURRENT app.
                observed = false; candidate = null; applied = null;
                Log?.Invoke("CONNECTED; HID stopped while checking foreground");
                StatusChanged?.Invoke(new(WatchState.WaitingForFocus));
            }
            var current = foreground(); // Always sampled after a reconnect, never cached across it.
            if (!observed || current != candidate)
            {
                if (applied is not null) device.Stop();
                applied = null; candidate = current; candidateSince = clock(); observed = true;
                StatusChanged?.Invoke(new(current is null ? WatchState.ForegroundUnavailable : WatchState.WaitingForFocus, current?.Executable));
                Log?.Invoke(current is null ? "PAUSED: foreground unavailable" : $"FOREGROUND {current.Executable}; waiting for stable focus");
                return;
            }
            if (current is null || clock() - candidateSince < StableMs) return;
            var profile = settings.Resolve(current.ExecutablePath);
            if (applied is null)
            {
                var state = device.Configure(profile.Left, profile.Right, profile.Enabled);
                if (!Matches(state, profile)) throw new InvalidDataException("Profile acknowledgement mismatch");
                // Focus can change while SET waits for its ACK. Stop immediately
                // and resample next tick instead of announcing the old profile.
                if (foreground() != current)
                {
                    device.Stop(); observed = false; candidate = null;
                    Log?.Invoke("PAUSED: foreground changed while applying profile");
                    StatusChanged?.Invoke(new(WatchState.WaitingForFocus));
                    return;
                }
                applied = profile; heartbeatAt = clock();
                StatusChanged?.Invoke(new(WatchState.Applied, current.Executable, profile));
                Log?.Invoke($"APPLIED app={current.Executable} profile={profile.Name} enabled={(profile.Enabled ? 1 : 0)} " +
                    $"left={profile.Left.Modifiers}:{profile.Left.Usage} right={profile.Right.Modifiers}:{profile.Right.Usage}");
            }
            else if (clock() - heartbeatAt >= HeartbeatMs)
            {
                if (!Matches(device.KeepAlive(), profile)) throw new InvalidDataException("Device profile changed unexpectedly");
                heartbeatAt = clock();
            }
        }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException or TimeoutException or InvalidOperationException)
        {
            // Release uncertain presses, discard all foreground/profile state,
            // then retry only this explicitly selected port after a delay.
            CloseDevice(); observed = false; candidate = null; applied = null;
            retryAt = clock() + ReconnectMs;
            StatusChanged?.Invoke(new(WatchState.Disconnected, Error: e.Message));
            Log?.Invoke($"DISCONNECTED: {e.Message}; retry in {ReconnectMs} ms");
        }
    }
    private static bool Matches(DeviceState state, ShortcutProfile profile) => state.Managed && state.LeaseActive &&
        state.Enabled == profile.Enabled && state.Left == profile.Left && state.Right == profile.Right;
    private void CloseDevice()
    {
        var old = device; device = null;
        if (old is null) return;
        try
        {
            if (identified)
            {
                old.Stop();
                Log?.Invoke("STOPPED: device acknowledged HID off");
                StatusChanged?.Invoke(new(WatchState.Stopped));
            }
        }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException or TimeoutException or InvalidOperationException)
        {
            Log?.Invoke($"STOP unavailable: {e.Message}; device heartbeat timeout will stop HID");
            StatusChanged?.Invoke(new(WatchState.Disconnected, Error: e.Message));
        }
        finally
        {
            identified = false;
            try { old.Dispose(); }
            catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException)
            { Log?.Invoke($"Serial close: {e.Message}"); }
        }
    }
    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        CloseDevice();
    }
}
