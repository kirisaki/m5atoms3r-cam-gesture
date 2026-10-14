using System.Collections.ObjectModel;

namespace GestureControl;

// Immutable map. Preserve explicit None rows for editing; compare only active bindings.
public sealed class GestureBindings : IEquatable<GestureBindings>
{
    public static IReadOnlyList<string> Paths { get; } = Array.AsReadOnly(EnumeratePaths().ToArray());
    private static IEnumerable<string> EnumeratePaths()
    {
        string[] directions = ["L", "R", "U", "D"];
        var paths = directions.AsEnumerable();
        for (var length = 1; length <= 3; ++length)
        {
            foreach (var path in paths) yield return path;
            paths = paths.SelectMany(p => directions.Where(d => p[^1] != d[0]).Select(d => p + d)).ToArray();
        }
    }
    public IReadOnlyDictionary<string, Binding> Entries { get; }
    public IReadOnlyDictionary<string, Binding> ActiveEntries { get; }
    public Binding this[string path] => Entries.TryGetValue(path, out var binding) ? binding : default;
    public bool IsLegacy => ActiveEntries.Keys.All(path => path is "L" or "R");
    public static GestureBindings Legacy(Binding left, Binding right) => new(new Dictionary<string, Binding> { ["L"] = left, ["R"] = right });
    public GestureBindings(IEnumerable<KeyValuePair<string, Binding>> entries)
    {
        var map = new SortedDictionary<string, Binding>(StringComparer.Ordinal);
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var (path, binding) in entries)
        {
            if (!ValidPath(path)) throw new ArgumentException($"Invalid gesture path: {path}");
            if (!seen.Add(path)) throw new ArgumentException($"Duplicate gesture path: {path}");
            if (!binding.IsValid) throw new ArgumentException($"Invalid shortcut for {path}");
            map.Add(path, binding);
        }
        Entries = new ReadOnlyDictionary<string, Binding>(map);
        ActiveEntries = new ReadOnlyDictionary<string, Binding>(map.Where(e => e.Value.Usage != 0).ToDictionary(e => e.Key, e => e.Value));
    }
    public static bool ValidPath(string path) => path.Length is >= 1 and <= 3 &&
        path.All(c => c is 'L' or 'R' or 'U' or 'D') &&
        !path.Zip(path.Skip(1), (a, b) => a == b).Any(equal => equal);
    public static string Display(string path) => string.Join(" ", path.Select(c => c switch
        { 'L' => "←", 'R' => "→", 'U' => "↑", 'D' => "↓", _ => "?" }));
    public bool Equals(GestureBindings? other) => other is not null && ActiveEntries.Count == other.ActiveEntries.Count &&
        ActiveEntries.All(pair => other.ActiveEntries.TryGetValue(pair.Key, out var value) && value == pair.Value);
    public override bool Equals(object? obj) => obj is GestureBindings other && Equals(other);
    public override int GetHashCode()
    {
        var hash = new HashCode();
        foreach (var pair in ActiveEntries) { hash.Add(pair.Key); hash.Add(pair.Value); }
        return hash.ToHashCode();
    }
}
