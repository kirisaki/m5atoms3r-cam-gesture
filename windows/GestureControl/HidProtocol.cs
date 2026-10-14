using System.Buffers.Binary;

namespace GestureControl;

public enum HidOperation : byte { Info = 1, Begin, Page, Commit, Keep, Stop, Standalone }
public sealed record HidReply(uint Id, HidOperation Operation, byte Page, byte Count, byte Flags, byte MaxSteps,
    uint Generation, IReadOnlyList<KeyValuePair<string, Binding>> Entries);

public static class HidProtocol
{
    public const int ReportSize = 64, EntriesPerPage = 15;
    public const byte ReportId = 6;
    public const ushort UsagePage = 0xff00, Usage = 0x53;
    public static byte PathCode(string path)
    {
        if (!GestureBindings.ValidPath(path)) throw new ArgumentException("Invalid gesture path");
        var code = 0;
        foreach (var c in path) code = code * 5 + " LRUD".IndexOf(c);
        return (byte)code;
    }
    public static string PathText(byte code)
    {
        var path = "";
        for (var remaining = (int)code; remaining != 0; remaining /= 5) path = " LRUD"[remaining % 5] + path;
        if (!GestureBindings.ValidPath(path)) throw new InvalidDataException("Invalid HID gesture path");
        return path;
    }
    public static byte[] Request(uint id, HidOperation operation, uint transaction = 0, byte page = 0,
        byte count = 0, bool enabled = false, IEnumerable<KeyValuePair<string, Binding>>? entries = null)
    {
        if (id == 0 || !Enum.IsDefined(operation) || count > 52) throw new ArgumentException("Invalid HID request");
        var report = new byte[ReportSize];
        report[0] = ReportId; "AGH1"u8.CopyTo(report.AsSpan(1));
        report[5] = (byte)operation; Write32(report, 7, id);
        report[11] = page; report[12] = count; report[13] = enabled ? (byte)2 : (byte)0;
        Write32(report, 15, transaction);
        var index = 0;
        foreach (var entry in entries ?? [])
        {
            if (index >= EntriesPerPage || !entry.Value.IsValid || entry.Value.Usage == 0) throw new ArgumentException("Invalid HID page");
            var offset = 19 + 3 * index++;
            report[offset] = PathCode(entry.Key); report[offset + 1] = entry.Value.Modifiers; report[offset + 2] = entry.Value.Usage;
        }
        return report;
    }
    public static bool HasSignature(byte[] report) => report.Length == ReportSize && report[0] == ReportId && report.AsSpan(1, 4).SequenceEqual("AGH1"u8);
    public static HidReply? Reply(byte[] report, uint id, HidOperation operation)
    {
        if (!HasSignature(report)) throw new InvalidDataException("Unexpected HID device or protocol");
        if (Read32(report, 7) != id) return null;
        if (report[5] != (byte)operation) throw new InvalidDataException("Unexpected HID operation acknowledgement");
        if (report[6] != 0) throw new DeviceException(report[6] switch
        {
            1 => "FORMAT", 2 => "RANGE", 3 => "PATH", 4 => "EXPIRED", 5 => "MEASUREMENT", 6 => "UNSUPPORTED_PATH", 7 => "CHANGED",
            _ => "UNKNOWN_HID_ERROR"
        });
        byte page = report[11], count = report[12], flags = report[13], maxSteps = report[14];
        if (count > 52 || page > 3 || (page != 0 && page * EntriesPerPage >= count) || (flags & ~15) != 0 || maxSteps is not (1 or 3) ||
            ((flags & 4) != 0 && (flags & 1) == 0) || ((flags & 3) == 3 && (flags & 4) == 0) || ((flags & 10) == 10))
            throw new InvalidDataException("Invalid HID state");
        var amount = Math.Min(EntriesPerPage, count - page * EntriesPerPage);
        var entries = new List<KeyValuePair<string, Binding>>();
        for (var i = 0; i < amount; ++i)
        {
            var offset = 19 + 3 * i;
            var path = PathText(report[offset]);
            var binding = new Binding(report[offset + 1], report[offset + 2]);
            if (!binding.IsValid || binding.Usage == 0 || (maxSteps == 1 && path is not ("L" or "R")))
                throw new InvalidDataException("Invalid HID binding");
            entries.Add(new(path, binding));
        }
        if (report.AsSpan(19 + 3 * amount).ContainsAnyExcept((byte)0)) throw new InvalidDataException("Invalid HID padding");
        return new(id, operation, page, count, flags, maxSteps, Read32(report, 15), entries);
    }
    private static uint Read32(byte[] data, int offset) => BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset, 4));
    private static void Write32(byte[] data, int offset, uint value) => BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(offset, 4), value);
}
