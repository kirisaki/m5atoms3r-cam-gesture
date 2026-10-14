namespace GestureControl;

// Owns the serial worker across UI start/pause/save/exit. Operations are serialized:
// an old worker must release its keys and close the port before a new one starts.
public sealed class WatchService : IAsyncDisposable
{
    private readonly SemaphoreSlim gate = new(1, 1);
    private CancellationTokenSource? cancellation;
    private Task? worker;
    private bool disposed;
    public bool IsRunning => worker is { IsCompleted: false };
    public Action<WatchStatus>? StatusChanged { get; set; }
    public Action<string>? Log { get; set; }
    public async Task StartAsync(ProfileSettings settings, Func<ForegroundApp?> foreground, Func<IManagedDevice> connect)
    {
        await gate.WaitAsync().ConfigureAwait(false);
        try
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            await StopCoreAsync().ConfigureAwait(false);
            cancellation = new();
            var token = cancellation.Token;
            worker = Task.Run(() =>
            {
                try
                {
                    using var watcher = new ProfileWatcher(settings, foreground, connect)
                    { Log = message => Log?.Invoke(message), StatusChanged = status => StatusChanged?.Invoke(status) };
                    while (!token.IsCancellationRequested)
                    {
                        watcher.Tick();
                        if (token.WaitHandle.WaitOne(ProfileWatcher.PollMs)) break;
                    }
                }
                catch (Exception e)
                {
                    Log?.Invoke($"Worker stopped: {e.Message}");
                    StatusChanged?.Invoke(new(WatchState.Faulted, Error: e.Message));
                }
            });
        }
        finally { gate.Release(); }
    }
    public async Task StopAsync()
    {
        await gate.WaitAsync().ConfigureAwait(false);
        try { await StopCoreAsync().ConfigureAwait(false); }
        finally { gate.Release(); }
    }
    private async Task StopCoreAsync()
    {
        if (cancellation is null) return;
        cancellation.Cancel();
        try { if (worker is not null) await worker.ConfigureAwait(false); }
        finally { cancellation.Dispose(); cancellation = null; worker = null; }
    }
    public async ValueTask DisposeAsync()
    {
        await gate.WaitAsync().ConfigureAwait(false);
        try { disposed = true; await StopCoreAsync().ConfigureAwait(false); }
        finally { gate.Release(); }
    }
}
