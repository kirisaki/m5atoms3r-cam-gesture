using System.Globalization;

namespace GestureControl;

public sealed record DeviceState(bool Managed, bool Enabled, bool LeaseActive, GestureBindings Bindings)
{
    public DeviceState(bool managed, bool enabled, bool leaseActive, Binding left, Binding right)
        : this(managed, enabled, leaseActive, GestureBindings.Legacy(left, right)) { }
    public Binding Left => Bindings["L"];
    public Binding Right => Bindings["R"];
    public int MaxSteps { get; init; } = 1;
}
public sealed class DeviceException(string code) : IOException($"Device rejected command: {code}")
{
    public string Code { get; } = code;
}

public static class Protocol
{
    public static string Frame(uint id, string command, int version = 1)
    {
        if (version is not (1 or 2) || id == 0 || command.Any(c => c is < ' ' or > '~')) throw new ArgumentException("Invalid command frame");
        return $"@G{version} {id.ToString(CultureInfo.InvariantCulture)} {command}\n";
    }
    public static string Set(Binding left, Binding right, bool enabled)
    {
        if (!left.IsValid || !right.IsValid) throw new ArgumentException("Invalid keyboard binding");
        return FormattableString.Invariant($"SET {left.Modifiers} {left.Usage} {right.Modifiers} {right.Usage} {(enabled ? 1 : 0)}");
    }
    public static string Set(GestureBindings bindings, bool enabled)
    {
        var entries = bindings.ActiveEntries.Select(e => FormattableString.Invariant($"{e.Key} {e.Value.Modifiers} {e.Value.Usage}"));
        return FormattableString.Invariant($"SET {(enabled ? 1 : 0)} {bindings.ActiveEntries.Count}") +
            (bindings.ActiveEntries.Count == 0 ? "" : " " + string.Join(' ', entries));
    }
    // Diagnostic/event lines and responses to old requests do not acknowledge a new command.
    public static DeviceState? Reply(string line, uint expectedId, int version = 1)
    {
        var p = line.TrimEnd('\r').Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (p.Length < 2 || p[0] is not ("@G1" or "@G2")) return null;
        if (!uint.TryParse(p[1], NumberStyles.None, CultureInfo.InvariantCulture, out var id)) return null;
        if (id != expectedId && id != 0) return null;
        if (p.Length == 4 && p[2] == "ERR") throw new DeviceException(p[3]);
        if (p[0] != $"@G{version}") return null;
        if (version == 2) return ReplyV2(p, id);
        if (id == 0 || p.Length != 11 || p[2] != "OK" || p[3] != "ATOMS3R_CAM" ||
            p[4] is not ("MANAGED" or "STANDALONE")) throw new InvalidDataException("Malformed device acknowledgement");
        bool Flag(int i) => p[i] switch { "0" => false, "1" => true, _ => throw new InvalidDataException("Invalid flag") };
        byte Number(int i) => byte.TryParse(p[i], NumberStyles.None, CultureInfo.InvariantCulture, out var n)
            ? n : throw new InvalidDataException("Invalid binding number");
        var left = new Binding(Number(7), Number(8));
        var right = new Binding(Number(9), Number(10));
        if (!left.IsValid || !right.IsValid) throw new InvalidDataException("Invalid device binding");
        var state = new DeviceState(p[4] == "MANAGED", Flag(5), Flag(6), left, right);
        if (!state.Left.IsValid || !state.Right.IsValid || (!state.Managed && state.LeaseActive) ||
            (state.Managed && state.Enabled && !state.LeaseActive)) throw new InvalidDataException("Invalid device state");
        return state;
    }
    private static DeviceState ReplyV2(string[] p, uint id)
    {
        if (id == 0 || p.Length < 9 || p[2] != "OK" || p[3] != "ATOMS3R_CAM" ||
            p[4] is not ("MANAGED" or "STANDALONE") || p[5] is not ("0" or "1") || p[6] is not ("0" or "1") ||
            p[7] is not ("1" or "3") || !int.TryParse(p[8], NumberStyles.None, CultureInfo.InvariantCulture, out var count) ||
            count is < 0 or > 52 || p.Length != 9 + count * 3)
            throw new InvalidDataException("Malformed gesture map acknowledgement");
        var entries = new List<KeyValuePair<string, Binding>>();
        for (var i = 0; i < count; ++i)
        {
            if (!byte.TryParse(p[10 + i * 3], NumberStyles.None, CultureInfo.InvariantCulture, out var modifiers) ||
                !byte.TryParse(p[11 + i * 3], NumberStyles.None, CultureInfo.InvariantCulture, out var usage))
                throw new InvalidDataException("Invalid binding number");
            entries.Add(new(p[9 + i * 3], new(modifiers, usage)));
        }
        GestureBindings bindings;
        try { bindings = new(entries); }
        catch (ArgumentException e) { throw new InvalidDataException("Invalid gesture map", e); }
        var state = new DeviceState(p[4] == "MANAGED", p[5] == "1", p[6] == "1", bindings) { MaxSteps = int.Parse(p[7], CultureInfo.InvariantCulture) };
        if ((!state.Managed && state.LeaseActive) || (state.Managed && state.Enabled && !state.LeaseActive) ||
            (state.MaxSteps == 1 && !bindings.IsLegacy)) throw new InvalidDataException("Invalid device state");
        return state;
    }

}
