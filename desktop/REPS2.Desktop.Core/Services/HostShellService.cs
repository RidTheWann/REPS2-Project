using System.Diagnostics;
using REPS2.Desktop.Api.Contracts;

namespace REPS2.Desktop.Core.Services;

public sealed class HostShellService(DesktopPaths paths) : IHostShellService
{
    public void OpenDirectory(string directoryPath)
    {
        var fullPath = Path.GetFullPath(directoryPath);
        var root = Path.GetFullPath(paths.Root);
        var relative = Path.GetRelativePath(root, fullPath);
        var outsideRoot = Path.IsPathRooted(relative)
            || relative == ".."
            || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
            || relative.StartsWith(".." + Path.AltDirectorySeparatorChar, StringComparison.Ordinal);
        if (outsideRoot)
            throw new InvalidOperationException("The requested directory is outside the REPS2 application data root.");

        if (!Directory.Exists(fullPath))
            throw new DirectoryNotFoundException($"The requested directory is no longer available: {fullPath}");

        Process.Start(new ProcessStartInfo { FileName = fullPath, UseShellExecute = true });
    }
}
