namespace REPS2.Desktop.Core.Services;

public sealed class DesktopPaths
{
    public DesktopPaths(string? localAppData = null)
    {
        var root = localAppData ?? Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrWhiteSpace(root))
            root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "AppData", "Local");

        Root = Path.Combine(root, "REPS2");
        Library = Path.Combine(Root, "Library");
        Builds = Path.Combine(Root, "Desktop", "Builds");
        Jobs = Path.Combine(Root, "Desktop", "Jobs");
        Logs = Path.Combine(Root, "Logs");
        Reports = Path.Combine(Root, "Desktop", "Reports");
        Updates = Path.Combine(Root, "Desktop", "Updates");
        UpdateSignals = Path.Combine(Updates, "Signals");
        Updater = Path.Combine(Root, "Updater");
        Settings = Path.Combine(Root, "Desktop", "settings.json");
        Toolchain = Path.Combine(Root, "SDK");
    }

    public string Root { get; }
    public string Library { get; }
    public string Builds { get; }
    public string Jobs { get; }
    public string Logs { get; }
    public string Reports { get; }
    public string Updates { get; }
    public string UpdateSignals { get; }
    public string Updater { get; }
    public string Settings { get; }
    public string Toolchain { get; }

    public void EnsureApplicationDirectories()
    {
        Directory.CreateDirectory(Root);
        Directory.CreateDirectory(Library);
        Directory.CreateDirectory(Builds);
        Directory.CreateDirectory(Jobs);
        Directory.CreateDirectory(Logs);
        Directory.CreateDirectory(Reports);
        Directory.CreateDirectory(Updates);
        Directory.CreateDirectory(UpdateSignals);
        Directory.CreateDirectory(Updater);
    }
}
