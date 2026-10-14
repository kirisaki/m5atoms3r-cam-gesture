using System.Globalization;

namespace GestureControl;

public sealed record DeviceState(bool Managed, bool Enabled, bool LeaseActive, Binding Left, Binding Right);
public sealed class DeviceException(string code) : IOException($"Device rejected command: {code}")
{
    public string Code { get; } = code;
}

public static class Protocol
{
    public static string Frame(uint id, string command)
    {
        if (id == 0 || command.Any(c => c is < ' ' or > '~')) throw new ArgumentException("Invalid command frame");
        return $"@G1 {id.ToString(CultureInfo.InvariantCulture)} {command}\n";
    }
    public static string Set(Binding left, Binding right, bool enabled)
    {
        if (!left.IsValid || !right.IsValid) throw new ArgumentException("Invalid keyboard binding");
        return FormattableString.Invariant($"SET {left.Modifiers} {left.Usage} {right.Modifiers} {right.Usage} {(enabled ? 1 : 0)}");
    }
    // Diagnostic/event lines and responses to old requests do not acknowledge a new command.
    public static DeviceState? Reply(string line, uint expectedId)
    {
        var p = line.TrimEnd('\r').Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (p.Length < 2 || p[0] != "@G1") return null;
        if (!uint.TryParse(p[1], NumberStyles.None, CultureInfo.InvariantCulture, out var id)) return null;
        if (id != expectedId && id != 0) return null;
        if (p.Length == 4 && p[2] == "ERR") throw new DeviceException(p[3]);
        if (id == 0 || p.Length != 11 || p[2] != "OK" || p[3] != "ATOMS3R_CAM" ||
            p[4] is not ("MANAGED" or "STANDALONE")) throw new InvalidDataException("Malformed device acknowledgement");
        bool Flag(int i) => p[i] switch { "0" => false, "1" => true, _ => throw new InvalidDataException("Invalid flag") };
        byte Number(int i) => byte.TryParse(p[i], NumberStyles.None, CultureInfo.InvariantCulture, out var n)
            ? n : throw new InvalidDataException("Invalid binding number");
        var state = new DeviceState(p[4] == "MANAGED", Flag(5), Flag(6), new(Number(7), Number(8)), new(Number(9), Number(10)));
        if (!state.Left.IsValid || !state.Right.IsValid || (!state.Managed && state.LeaseActive) ||
            (state.Managed && state.Enabled && !state.LeaseActive)) throw new InvalidDataException("Invalid device state");
        return state;
    }
}
