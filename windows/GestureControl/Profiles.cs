using System.Text.Json;
using System.Text.Json.Serialization;

namespace GestureControl;

public sealed record ShortcutProfile(string Name, GestureBindings Bindings, bool Enabled)
{
    public ShortcutProfile(string name, Binding left, Binding right, bool enabled)
        : this(name, GestureBindings.Legacy(left, right), enabled) { }
    public Binding Left => Bindings["L"];
    public Binding Right => Bindings["R"];
}

public sealed class ProfileSettings
{
    private readonly Dictionary<string, ShortcutProfile> applications;
    public ShortcutProfile Default { get; }
    public IReadOnlyCollection<ShortcutProfile> Applications => applications.Values;
    private ProfileSettings(ShortcutProfile fallback, Dictionary<string, ShortcutProfile> applications)
        => (Default, this.applications) = (fallback, applications);

    public ShortcutProfile Resolve(string executablePath)
    {
        var name = ExecutableName(executablePath);
        return applications.TryGetValue(name, out var profile) ? profile : Default;
    }
    public static string ExecutableName(string path) => path.Replace('\\', '/').Split('/')[^1];
    public static ProfileSettings Load(string path) => Parse(File.ReadAllText(path));
    public string ToJson()
    {
        static object Profile(ShortcutProfile p) => new { enabled = p.Enabled, bindings = p.Bindings.Entries.ToDictionary(e => e.Key, e => e.Value.ToShortcutString()) };
        return JsonSerializer.Serialize(new
        {
            version = 2,
            @default = Profile(Default),
            applications = applications.Values.Select(p => new
            {
                executable = p.Name, enabled = p.Enabled, bindings = p.Bindings.Entries.ToDictionary(e => e.Key, e => e.Value.ToShortcutString())
            })
        }, new JsonSerializerOptions { WriteIndented = true });
    }
    public static ProfileSettings Parse(string json)
    {
        var options = new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
        };
        var document = JsonSerializer.Deserialize<SettingsDocument>(json, options)
            ?? throw new InvalidDataException("Settings must be a JSON object");
        if (document.Version is not (1 or 2) || document.Default is null || document.Applications is null)
            throw new InvalidDataException("Settings require version 1 or 2, default and applications");
        var entries = new Dictionary<string, ShortcutProfile>(StringComparer.OrdinalIgnoreCase);
        foreach (var item in document.Applications)
        {
            if (item is null || string.IsNullOrWhiteSpace(item.Executable) ||
                !item.Executable.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ||
                item.Executable.IndexOfAny(['/', '\\', ':', '*', '?', '"', '<', '>', '|']) >= 0 ||
                item.Executable.Any(char.IsControl) || item.Executable.Trim() != item.Executable)
                throw new InvalidDataException("Application executable must be a file name such as notepad.exe");
            if (!entries.TryAdd(item.Executable, Compile(item.Executable, item, document.Version)))
                throw new InvalidDataException($"Duplicate application: {item.Executable}");
        }
        return new(Compile("default", document.Default, document.Version), entries);
    }
    private static ShortcutProfile Compile(string name, ProfileDocument document, int version)
    {
        if (document.Enabled is null) throw new InvalidDataException($"Profile {name} requires enabled");
        try
        {
            if (version == 1)
            {
                if (document.Left is null || document.Right is null || document.Bindings.HasValue)
                    throw new InvalidDataException($"Profile {name} requires left and right (version 1)");
                return new(name, Binding.Parse(document.Left), Binding.Parse(document.Right), document.Enabled.Value);
            }
            if (document.Left is not null || document.Right is not null ||
                document.Bindings is not { ValueKind: JsonValueKind.Object } bindings)
                throw new InvalidDataException($"Profile {name} requires bindings (version 2)");
            // Enumerate JSON properties, rather than a dictionary, so duplicates cannot silently win.
            var pairs = bindings.EnumerateObject().Select(property =>
                new KeyValuePair<string, Binding>(property.Name,
                    property.Value.ValueKind == JsonValueKind.String ? Binding.Parse(property.Value.GetString()!) :
                    throw new InvalidDataException($"Shortcut for {property.Name} must be a string")));
            return new(name, new GestureBindings(pairs), document.Enabled.Value);
        }
        catch (ArgumentException e) { throw new InvalidDataException($"Profile {name}: {e.Message}", e); }
    }
    private sealed class SettingsDocument
    {
        public int Version { get; set; }
        public ProfileDocument? Default { get; set; }
        public List<ApplicationDocument?>? Applications { get; set; }
    }
    private class ProfileDocument
    {
        public JsonElement? Bindings { get; set; }
        public string? Left { get; set; }
        public string? Right { get; set; }
        public bool? Enabled { get; set; }
    }
    private sealed class ApplicationDocument : ProfileDocument
    {
        public string? Executable { get; set; }
    }
}
