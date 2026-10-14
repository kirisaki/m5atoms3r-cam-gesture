using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace GestureControl;

public sealed record ForegroundApp(nint Window, uint ProcessId, string ExecutablePath)
{
    public string Executable => ProfileSettings.ExecutableName(ExecutablePath);
}

[SupportedOSPlatform("windows")]
public static class WindowsForeground
{
    public static ForegroundApp? Current()
    {
        // A secure/locked or different input desktop must not reuse the last app.
        if (!OnInputDesktop()) return null;
        var window = GetForegroundWindow();
        if (window == 0 || GetWindowThreadProcessId(window, out var pid) == 0 || pid == 0) return null;
        using var process = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, false, pid);
        if (process.IsInvalid) return null;
        var path = new StringBuilder(32768);
        var size = path.Capacity;
        if (!QueryFullProcessImageName(process, 0, path, ref size) || size == 0) return null;
        // Window/process may have changed while querying the process handle.
        if (GetForegroundWindow() != window || GetWindowThreadProcessId(window, out var currentPid) == 0 || currentPid != pid)
            return null;
        return new(window, pid, path.ToString());
    }
    private static bool OnInputDesktop()
    {
        var input = OpenInputDesktop(0, false, 1 /* DESKTOP_READOBJECTS */);
        if (input == 0) return false;
        try
        {
            var thread = GetThreadDesktop(GetCurrentThreadId());
            return DesktopName(input) is { } inputName && inputName == DesktopName(thread);
        }
        finally { CloseDesktop(input); }
    }
    private static string? DesktopName(nint desktop)
    {
        var name = new StringBuilder(256);
        return GetUserObjectInformation(desktop, 2 /* UOI_NAME */, name, name.Capacity * 2, out _)
            ? name.ToString() : null;
    }
    [DllImport("user32.dll")] private static extern nint GetForegroundWindow();
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(nint window, out uint processId);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeProcessHandle OpenProcess(uint access, [MarshalAs(UnmanagedType.Bool)] bool inherit, uint processId);
    [DllImport("kernel32.dll", EntryPoint = "QueryFullProcessImageNameW", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool QueryFullProcessImageName(SafeProcessHandle process, uint flags, StringBuilder name, ref int size);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern nint OpenInputDesktop(uint flags, [MarshalAs(UnmanagedType.Bool)] bool inherit, uint access);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool CloseDesktop(nint desktop);
    [DllImport("user32.dll")] private static extern nint GetThreadDesktop(uint threadId);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll", EntryPoint = "GetUserObjectInformationW", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetUserObjectInformation(nint handle, int index, StringBuilder information, int length, out int needed);
}
