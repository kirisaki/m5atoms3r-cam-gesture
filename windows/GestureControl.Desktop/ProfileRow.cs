using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace GestureControl.Desktop;

public sealed record GestureChoice(string Path, string Label)
{
    public static IReadOnlyList<GestureChoice> All { get; } = GestureBindings.Paths.Select(p => new GestureChoice(p, GestureBindings.Display(p))).ToArray();
}
public sealed class GestureRow : INotifyPropertyChanged
{
    private string path, shortcut;
    public string Path { get => path; set { path = value; Changed(); } }
    public string Shortcut { get => shortcut; set { shortcut = value; Changed(); } }
    public GestureRow(string path, string shortcut) => (this.path, this.shortcut) = (path, shortcut);
    public event PropertyChangedEventHandler? PropertyChanged;
    private void Changed([CallerMemberName] string? property = null) => PropertyChanged?.Invoke(this, new(property));
}
public sealed class ProfileRow : INotifyPropertyChanged
{
    private string executable;
    private bool enabled;
    public bool IsDefault { get; }
    public string Executable { get => executable; set { executable = value; Changed(); } }
    public bool Enabled { get => enabled; set { enabled = value; Changed(); } }
    public ObservableCollection<GestureRow> Gestures { get; } = [];
    public ProfileRow(ShortcutProfile profile, bool isDefault = false)
    {
        IsDefault = isDefault;
        executable = isDefault ? "既定（その他）" : profile.Name;
        enabled = profile.Enabled;
        foreach (var path in new[] { "L", "R", "U", "D" }.Concat(profile.Bindings.Entries.Keys.Where(p => p.Length > 1)))
            AddGesture(path, profile.Bindings[path].ToShortcutString());
    }
    public void AddGesture(string path, string shortcut = "None")
    {
        var row = new GestureRow(path, shortcut);
        row.PropertyChanged += GestureChanged;
        Gestures.Add(row); Changed(nameof(Gestures));
    }
    public void RemoveGesture(GestureRow row)
    {
        row.PropertyChanged -= GestureChanged;
        Gestures.Remove(row); Changed(nameof(Gestures));
    }
    private void GestureChanged(object? sender, PropertyChangedEventArgs e) => Changed(nameof(Gestures));
    public Dictionary<string, string> ReadBindings()
    {
        var result = new Dictionary<string, string>();
        foreach (var row in Gestures)
        {
            if (!GestureBindings.ValidPath(row.Path)) throw new InvalidDataException("ジェスチャは上下左右の1〜3ステップで指定してください。");
            if (!result.TryAdd(row.Path, row.Shortcut))
                throw new InvalidDataException($"{Executable}: {GestureBindings.Display(row.Path)} が重複しています。");
        }
        return result;
    }
    public event PropertyChangedEventHandler? PropertyChanged;
    private void Changed([CallerMemberName] string? property = null) => PropertyChanged?.Invoke(this, new(property));
}
