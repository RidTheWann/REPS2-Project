using System.Diagnostics;

namespace REPS2.Desktop.Core.Services;

public sealed class UpdateLauncher(DesktopPaths paths)
{
    public async Task<string> EnsureUpdaterHelperAsync(CancellationToken cancellationToken = default)
    {
        var packagedHelper = Path.Combine(AppContext.BaseDirectory, "updater", "REPS2.Desktop.Updater.exe");
        if (!File.Exists(packagedHelper))
            throw new FileNotFoundException("The REPS2 update helper is not included in this installation.", packagedHelper);

        Directory.CreateDirectory(paths.Updater);
        var installedHelper = Path.Combine(paths.Updater, "REPS2.Desktop.Updater.exe");
        var temporary = installedHelper + ".new";
        await using (var source = File.OpenRead(packagedHelper))
        await using (var target = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None,
                         64 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan))
        {
            await source.CopyToAsync(target, cancellationToken);
            await target.FlushAsync(cancellationToken);
        }
        File.Move(temporary, installedHelper, overwrite: true);
        return installedHelper;
    }

    public async Task StartApplyAsync(
        string packagePath,
        string expectedSha256,
        CancellationToken cancellationToken = default)
    {
        var helper = await EnsureUpdaterHelperAsync(cancellationToken);
        var installDirectory = Path.TrimEndingDirectorySeparator(Path.GetFullPath(AppContext.BaseDirectory));
        var parentDirectory = Directory.GetParent(installDirectory)?.FullName
            ?? throw new InvalidOperationException("The REPS2 installation directory has no writable parent.");
        var permissionProbe = Path.Combine(parentDirectory, ".reps2-update-permission-" + Guid.NewGuid().ToString("N"));
        try
        {
            await File.WriteAllTextAsync(permissionProbe, "update permission probe", cancellationToken);
            File.Delete(permissionProbe);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            try { if (File.Exists(permissionProbe)) File.Delete(permissionProbe); } catch (IOException) { }
            throw new UnauthorizedAccessException(
                "Automatic updates need a user-writable install location. Run Install-REPS2.ps1 once, or move REPS2 to %LOCALAPPDATA%\\Programs\\REPS2.", exception);
        }

        var signalPath = Path.Combine(paths.UpdateSignals, Guid.NewGuid().ToString("N") + ".ok");
        var start = new ProcessStartInfo
        {
            FileName = helper,
            WorkingDirectory = paths.Updater,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        start.ArgumentList.Add("--wait-pid");
        start.ArgumentList.Add(Environment.ProcessId.ToString());
        start.ArgumentList.Add("--install-dir");
        start.ArgumentList.Add(installDirectory);
        start.ArgumentList.Add("--archive");
        start.ArgumentList.Add(Path.GetFullPath(packagePath));
        start.ArgumentList.Add("--sha256");
        start.ArgumentList.Add(expectedSha256);
        start.ArgumentList.Add("--exe");
        start.ArgumentList.Add("REPS2.Desktop.exe");
        start.ArgumentList.Add("--startup-signal");
        start.ArgumentList.Add(signalPath);

        var process = Process.Start(start)
            ?? throw new InvalidOperationException("The REPS2 update helper could not be started.");
        process.Dispose();
    }
}

public static class UpdateStartupSignal
{
    private const string EnvironmentVariable = "REPS2_UPDATE_STARTUP_SIGNAL";

    public static async Task SignalReadyAsync(DesktopPaths paths, CancellationToken cancellationToken = default)
    {
        var requested = Environment.GetEnvironmentVariable(EnvironmentVariable);
        if (string.IsNullOrWhiteSpace(requested))
            return;

        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(paths.UpdateSignals)) + Path.DirectorySeparatorChar;
        var full = Path.GetFullPath(requested);
        if (!full.StartsWith(root, StringComparison.OrdinalIgnoreCase)
            || !string.Equals(Path.GetExtension(full), ".ok", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The update startup signal path is outside the expected application data directory.");

        Directory.CreateDirectory(paths.UpdateSignals);
        await File.WriteAllTextAsync(full, "REPS2 Desktop startup completed.", cancellationToken);
        Environment.SetEnvironmentVariable(EnvironmentVariable, null);
    }
}
