using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

namespace GestureControl;

public sealed record DesktopSettings(string Port, ProfileSettings Profiles)
{
    public static string ValidatePort(string port, bool required = true)
    {
        var normalized = port.Trim().ToUpperInvariant();
        if (!required && normalized.Length == 0) return "";
        if (!Regex.IsMatch(normalized, "^COM[1-9][0-9]{0,4}$", RegexOptions.CultureInvariant))
            throw new InvalidDataException("カメラのCOMポートを選択してください（例：COM10）。");
        return normalized;
    }
    public string ToJson()
    {
        using var profiles = JsonDocument.Parse(Profiles.ToJson());
        return JsonSerializer.Serialize(new { version = 1, port = ValidatePort(Port, false), profiles = profiles.RootElement },
            new JsonSerializerOptions { WriteIndented = true });
    }
    public static DesktopSettings Parse(string json)
    {
        var data = JsonSerializer.Deserialize<Document>(json, new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase, UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
        }) ?? throw new InvalidDataException("設定ファイルが空です。");
        if (data.Version != 1 || data.Port is null || data.Profiles.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException("設定ファイルの形式に対応していません。");
        return new(ValidatePort(data.Port, false), ProfileSettings.Parse(data.Profiles.GetRawText()));
    }
    public static DesktopSettings Load(string path) => Parse(File.ReadAllText(path));
    public void Save(string path)
    {
        // Validate before touching the existing file. Replace only a fully flushed
        // file on the same volume; keep one previous version for recovery.
        var json = ToJson();
        Parse(json);
        var fullPath = Path.GetFullPath(path);
        Directory.CreateDirectory(Path.GetDirectoryName(fullPath)!);
        var temporary = fullPath + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                stream.Write(Encoding.UTF8.GetBytes(json + "\n"));
                stream.Flush(true);
            }
            if (File.Exists(fullPath)) File.Replace(temporary, fullPath, fullPath + ".bak");
            else File.Move(temporary, fullPath);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
    private sealed class Document
    {
        public int Version { get; set; }
        public string? Port { get; set; }
        public JsonElement Profiles { get; set; }
    }
}
