using System.Globalization;

namespace GestureControl;

// USB HID keyboard usage, not a Windows virtual-key code or a Unicode character.
public readonly record struct Binding(byte Modifiers, byte Usage)
{
    public static Binding Left => new(4, 80);
    public static Binding Right => new(4, 79);
    public bool IsValid => Usage == 0 ? Modifiers == 0 : Usage is >= 4 and <= 0x73;

    public string ToShortcutString()
    {
        if (this == default) return "None";
        var key = Usage switch
        {
            >= 4 and <= 29 => ((char)('A' + Usage - 4)).ToString(),
            >= 30 and <= 38 => ((char)('1' + Usage - 30)).ToString(),
            >= 58 and <= 69 => $"F{Usage - 57}",
            >= 104 and <= 115 => $"F{Usage - 91}",
            39 => "0", 40 => "Enter", 41 => "Esc", 42 => "Backspace", 43 => "Tab", 44 => "Space",
            73 => "Insert", 74 => "Home", 75 => "PageUp", 76 => "Delete", 77 => "End", 78 => "PageDown",
            79 => "Right", 80 => "Left", 81 => "Down", 82 => "Up",
            _ => throw new InvalidDataException($"Unsupported shortcut usage: {Usage}")
        };
        string[] names = ["Ctrl", "Shift", "Alt", "Win", "RCtrl", "RShift", "RAlt", "RWin"];
        var modifiers = Modifiers;
        var parts = names.Where((_, i) => (modifiers & (1 << i)) != 0).Append(key);
        return string.Join('+', parts);
    }

    public static Binding Parse(string text)
    {
        if (text.Any(char.IsControl)) throw new ArgumentException("Control characters are not valid in a shortcut");
        if (text.Equals("None", StringComparison.OrdinalIgnoreCase)) return default;
        var parts = text.Split('+', StringSplitOptions.TrimEntries);
        byte modifiers = 0;
        foreach (var part in parts[..^1])
        {
            byte bit = part.ToUpperInvariant() switch
            {
                "CTRL" => 1, "SHIFT" => 2, "ALT" => 4, "WIN" => 8,
                "RCTRL" => 16, "RSHIFT" => 32, "RALT" => 64, "RWIN" => 128,
                _ => throw new ArgumentException($"Unknown modifier: {part}")
            };
            if ((modifiers & bit) != 0) throw new ArgumentException($"Duplicate modifier: {part}");
            modifiers |= bit;
        }
        var key = parts[^1].ToUpperInvariant();
        byte usage;
        if (key.Length == 1 && key[0] is >= 'A' and <= 'Z') usage = (byte)(4 + key[0] - 'A');
        else if (key.Length == 1 && key[0] is >= '1' and <= '9') usage = (byte)(30 + key[0] - '1');
        else if (key.Length is 2 or 3 && key.StartsWith('F') &&
                 int.TryParse(key.AsSpan(1), NumberStyles.None, CultureInfo.InvariantCulture, out var f) && f is >= 1 and <= 24)
            usage = (byte)(f <= 12 ? 57 + f : 91 + f);
        else usage = key switch
        {
            "0" => 39, "ENTER" => 40, "ESC" => 41, "BACKSPACE" => 42, "TAB" => 43, "SPACE" => 44,
            "INSERT" => 73, "HOME" => 74, "PAGEUP" => 75, "DELETE" => 76, "END" => 77, "PAGEDOWN" => 78,
            "RIGHT" => 79, "LEFT" => 80, "DOWN" => 81, "UP" => 82,
            _ => throw new ArgumentException($"Unknown key: {parts[^1]}")
        };
        return new(modifiers, usage);
    }
}
