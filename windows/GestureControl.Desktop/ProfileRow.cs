using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace GestureControl.Desktop;

public sealed class ProfileRow : INotifyPropertyChanged
{
    private string executable, left, right;
    private bool enabled;
    public bool IsDefault { get; }
    public string Executable { get => executable; set { executable = value; Changed(); } }
    public string Left { get => left; set { left = value; Changed(); } }
    public string Right { get => right; set { right = value; Changed(); } }
    public bool Enabled { get => enabled; set { enabled = value; Changed(); } }
    public ProfileRow(ShortcutProfile profile, bool isDefault = false)
    {
        IsDefault = isDefault;
        executable = isDefault ? "既定（その他のアプリ）" : profile.Name;
        left = profile.Left.ToShortcutString(); right = profile.Right.ToShortcutString(); enabled = profile.Enabled;
    }
    public event PropertyChangedEventHandler? PropertyChanged;
    private void Changed([CallerMemberName] string? property = null) => PropertyChanged?.Invoke(this, new(property));
}
