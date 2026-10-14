using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace GestureControl;

public sealed record HidCamera(string SerialNumber, string DevicePath)
{
    public string DisplayName => $"AtomS3R-CAM · {SerialNumber}";
}

public sealed class WindowsHidTransport : IFeatureTransport
{
    private readonly SafeFileHandle handle;
    private readonly Mutex ownership;
    private bool disposed;
    private WindowsHidTransport(HidCamera camera)
    {
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(camera.DevicePath.ToUpperInvariant())));
        ownership = new Mutex(false, @"Local\AtomS3RGesture.HID." + hash);
        var acquired = false;
        try
        {
            try { acquired = ownership.WaitOne(0); } catch (AbandonedMutexException) { acquired = true; }
            if (!acquired) throw new IOException("別のアプリがカメラを使用しています。");
            handle = Native.CreateFile(camera.DevicePath, 0xc0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
            if (handle.IsInvalid) { handle.Dispose(); throw Error("Open HID camera"); }
        }
        catch { if (acquired) ownership.ReleaseMutex(); ownership.Dispose(); throw; }
    }
    public static WindowsHidTransport Open(string? serialNumber = null)
    {
        var cameras = FindCameras().Where(c => string.IsNullOrEmpty(serialNumber) || c.SerialNumber.Equals(serialNumber, StringComparison.OrdinalIgnoreCase)).ToArray();
        if (cameras.Length == 0) throw new IOException("対応カメラが見つかりません。USB接続とHID対応ファームウェアを確認してください。");
        if (cameras.Length > 1) throw new IOException("カメラが複数あります。使用するカメラを選択してください。");
        return new(cameras[0]);
    }
    public static IReadOnlyList<HidCamera> FindCameras()
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("Windows HID transport requires Windows");
        Native.HidD_GetHidGuid(out var guid);
        var set = Native.SetupDiGetClassDevs(ref guid, null, IntPtr.Zero, 0x12);
        if (set == new IntPtr(-1)) throw Error("Enumerate HID devices");
        var cameras = new List<HidCamera>();
        try
        {
            for (uint index = 0; ; ++index)
            {
                var data = new Native.InterfaceData { Size = Marshal.SizeOf<Native.InterfaceData>() };
                if (!Native.SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref guid, index, ref data))
                {
                    if (Marshal.GetLastWin32Error() == 259) break;
                    throw Error("Enumerate HID interfaces");
                }
                Native.SetupDiGetDeviceInterfaceDetail(set, ref data, IntPtr.Zero, 0, out var size, IntPtr.Zero);
                if (size < 6) continue;
                var detail = Marshal.AllocHGlobal((int)size);
                string? path;
                try
                {
                    Marshal.WriteInt32(detail, IntPtr.Size == 8 ? 8 : 6);
                    if (!Native.SetupDiGetDeviceInterfaceDetail(set, ref data, detail, size, out _, IntPtr.Zero)) continue;
                    path = Marshal.PtrToStringUni(detail + 4);
                }
                finally { Marshal.FreeHGlobal(detail); }
                if (string.IsNullOrEmpty(path)) continue;
                using var probe = Native.CreateFile(path, 0, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
                if (probe.IsInvalid) continue;
                var attributes = new Native.Attributes { Size = Marshal.SizeOf<Native.Attributes>() };
                if (!Native.HidD_GetAttributes(probe, ref attributes) || attributes.VendorId != 0x303a || attributes.ProductId != 0x1001) continue;
                if (!Native.HidD_GetPreparsedData(probe, out var preparsed)) continue;
                try
                {
                    if (Native.HidP_GetCaps(preparsed, out var caps) != 0x00110000 || caps.UsagePage != HidProtocol.UsagePage ||
                        caps.Usage != HidProtocol.Usage || caps.FeatureReportByteLength != HidProtocol.ReportSize) continue;
                }
                finally { Native.HidD_FreePreparsedData(preparsed); }
                var serialBytes = new byte[256];
                if (!Native.HidD_GetSerialNumberString(probe, serialBytes, serialBytes.Length)) continue;
                var serial = Encoding.Unicode.GetString(serialBytes).TrimEnd('\0');
                if (string.IsNullOrWhiteSpace(serial)) continue;
                var report = new byte[HidProtocol.ReportSize]; report[0] = HidProtocol.ReportId;
                if (!Native.HidD_GetFeature(probe, report, report.Length) || !HidProtocol.HasSignature(report)) continue;
                cameras.Add(new(serial, path));
            }
        }
        finally { Native.SetupDiDestroyDeviceInfoList(set); }
        return cameras;
    }
    public void SetFeature(byte[] report)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (!HidProtocol.HasSignature(report)) throw new ArgumentException("Invalid feature report");
        if (!Native.HidD_SetFeature(handle, report, report.Length)) throw Error("Send HID feature");
    }
    public byte[] GetFeature()
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        var report = new byte[HidProtocol.ReportSize]; report[0] = HidProtocol.ReportId;
        if (!Native.HidD_GetFeature(handle, report, report.Length)) throw Error("Read HID feature");
        return report;
    }
    public void Dispose()
    {
        if (disposed) return;
        disposed = true; handle.Dispose(); ownership.ReleaseMutex(); ownership.Dispose();
    }
    private static IOException Error(string operation) => new(operation + ": " + new Win32Exception(Marshal.GetLastWin32Error()).Message);
    private static class Native
    {
        [StructLayout(LayoutKind.Sequential)] internal struct InterfaceData { public int Size; public Guid ClassGuid; public int Flags; public IntPtr Reserved; }
        [StructLayout(LayoutKind.Sequential)] internal struct Attributes { public int Size; public ushort VendorId, ProductId, Version; }
        [StructLayout(LayoutKind.Sequential)] internal struct Caps
        {
            public ushort Usage, UsagePage, InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] public ushort[] Reserved;
            public ushort NumberLinkCollectionNodes, NumberInputButtonCaps, NumberInputValueCaps, NumberInputDataIndices,
                NumberOutputButtonCaps, NumberOutputValueCaps, NumberOutputDataIndices, NumberFeatureButtonCaps, NumberFeatureValueCaps, NumberFeatureDataIndices;
        }
        [DllImport("hid.dll")] internal static extern void HidD_GetHidGuid(out Guid guid);
        [DllImport("hid.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.U1)] internal static extern bool HidD_GetAttributes(SafeFileHandle handle, ref Attributes attributes);
        [DllImport("hid.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.U1)] internal static extern bool HidD_GetPreparsedData(SafeFileHandle handle, out IntPtr data);
        [DllImport("hid.dll")] [return: MarshalAs(UnmanagedType.U1)] internal static extern bool HidD_FreePreparsedData(IntPtr data);
        [DllImport("hid.dll")] internal static extern int HidP_GetCaps(IntPtr data, out Caps caps);
        [DllImport("hid.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.U1)] internal static extern bool HidD_GetSerialNumberString(SafeFileHandle handle, [Out] byte[] buffer, int length);
        [DllImport("hid.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.U1)] internal static extern bool HidD_GetFeature(SafeFileHandle handle, [In, Out] byte[] buffer, int length);
        [DllImport("hid.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.U1)] internal static extern bool HidD_SetFeature(SafeFileHandle handle, byte[] buffer, int length);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] internal static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)] internal static extern IntPtr SetupDiGetClassDevs(ref Guid guid, string? enumerator, IntPtr parent, uint flags);
        [DllImport("setupapi.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool SetupDiEnumDeviceInterfaces(IntPtr set, IntPtr device, ref Guid guid, uint index, ref InterfaceData data);
        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr set, ref InterfaceData data, IntPtr detail, uint size, out uint requiredSize, IntPtr deviceInfo);
        [DllImport("setupapi.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
    }
}
