using System.Windows;

namespace GestureControl.Desktop;

public partial class App : Application
{
    private Mutex? instance;
    private bool ownsInstance;
    private EventWaitHandle? activate;
    private RegisteredWaitHandle? activationWait;
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        instance = new Mutex(true, @"Local\AtomS3RGesture.Desktop", out ownsInstance);
        if (!ownsInstance)
        {
            using var signal = new EventWaitHandle(false, EventResetMode.AutoReset, @"Local\AtomS3RGesture.Activate");
            signal.Set();
            Shutdown(); return;
        }
        try
        {
            string? port = null;
            var settings = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AtomS3RGesture", "settings.json");
            for (var i = 0; i < e.Args.Length; ++i)
            {
                switch (e.Args[i])
                {
                    case "--port" when i + 1 < e.Args.Length: port = DesktopSettings.ValidatePort(e.Args[++i]); break;
                    case "--settings" when i + 1 < e.Args.Length: settings = Path.GetFullPath(e.Args[++i]); break;
                    default: throw new ArgumentException("引数: [--port COM10] [--settings 設定ファイル]");
                }
            }
            MainWindow = new MainWindow(settings, port);
            MainWindow.Show();
            activate = new EventWaitHandle(false, EventResetMode.AutoReset, @"Local\AtomS3RGesture.Activate");
            activationWait = ThreadPool.RegisterWaitForSingleObject(activate, (_, _) =>
            {
                if (!Dispatcher.HasShutdownStarted) Dispatcher.BeginInvoke(() => ((MainWindow)MainWindow).RestoreWindow());
            }, null, Timeout.Infinite, false);
        }
        catch (Exception error)
        {
            MessageBox.Show(error.Message, "起動できません", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
        }
    }
    protected override void OnSessionEnding(SessionEndingCancelEventArgs e)
    {
        if (MainWindow is MainWindow window) window.StopForSessionEnd();
        base.OnSessionEnding(e);
    }
    protected override void OnExit(ExitEventArgs e)
    {
        activationWait?.Unregister(null);
        activate?.Dispose();
        if (ownsInstance) instance?.ReleaseMutex();
        instance?.Dispose();
        base.OnExit(e);
    }
}
