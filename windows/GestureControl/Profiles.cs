using System.Text.Json;
using System.Text.Json.Serialization;

namespace GestureControl;

public sealed record ShortcutProfile(string Name, Binding Left, Binding Right, bool Enabled);

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
        static object Profile(ShortcutProfile p) => new { enabled = p.Enabled, left = p.Left.ToShortcutString(), right = p.Right.ToShortcutString() };
        return JsonSerializer.Serialize(new
        {
            version = 1,
            @default = Profile(Default),
            applications = applications.Values.Select(p => new
            {
                executable = p.Name, enabled = p.Enabled, left = p.Left.ToShortcutString(), right = p.Right.ToShortcutString()
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
        if (document.Version != 1 || document.Default is null || document.Applications is null)
            throw new InvalidDataException("Settings require version 1, default and applications");
        var entries = new Dictionary<string, ShortcutProfile>(StringComparer.OrdinalIgnoreCase);
        foreach (var item in document.Applications)
        {
            if (item is null || string.IsNullOrWhiteSpace(item.Executable) ||
                !item.Executable.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ||
                item.Executable.IndexOfAny(['/', '\\', ':', '*', '?', '"', '<', '>', '|']) >= 0 ||
                item.Executable.Any(char.IsControl) || item.Executable.Trim() != item.Executable)
                throw new InvalidDataException("Application executable must be a file name such as notepad.exe");
            if (!entries.TryAdd(item.Executable, Compile(item.Executable, item)))
                throw new InvalidDataException($"Duplicate application: {item.Executable}");
        }
        return new(Compile("default", document.Default), entries);
    }
    private static ShortcutProfile Compile(string name, ProfileDocument document)
    {
        if (document.Left is null || document.Right is null || document.Enabled is null)
            throw new InvalidDataException($"Profile {name} requires left, right and enabled");
        try { return new(name, Binding.Parse(document.Left), Binding.Parse(document.Right), document.Enabled.Value); }
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
        public string? Left { get; set; }
        public string? Right { get; set; }
        public bool? Enabled { get; set; }
    }
    private sealed class ApplicationDocument : ProfileDocument
    {
        public string? Executable { get; set; }
    }
}
