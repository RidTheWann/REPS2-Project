using REPS2.Desktop.Core.Services;
using Xunit;

namespace REPS2.Desktop.Core.Tests;

public sealed class HostShellServiceTests : IDisposable
{
    private readonly string _localAppData = Path.Combine(Path.GetTempPath(), "reps2-hostshell-" + Guid.NewGuid().ToString("N"));
    private readonly DesktopPaths _paths;

    public HostShellServiceTests()
    {
        _paths = new DesktopPaths(_localAppData);
        Directory.CreateDirectory(_paths.Root);
    }

    public void Dispose()
    {
        try { Directory.Delete(_localAppData, recursive: true); } catch (IOException) { }
    }

    [Fact]
    public void RejectsADirectoryOutsideTheApplicationRoot()
    {
        var service = new HostShellService(_paths);
        var outside = Path.GetFullPath(Path.Combine(_paths.Root, ".."));

        var exception = Assert.Throws<InvalidOperationException>(() => service.OpenDirectory(outside));
        Assert.Contains("outside the REPS2 application data root", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RejectsAMissingDirectoryInsideTheApplicationRoot()
    {
        var service = new HostShellService(_paths);
        var missing = Path.Combine(_paths.Root, "Jobs", "does-not-exist");

        Assert.Throws<DirectoryNotFoundException>(() => service.OpenDirectory(missing));
    }
}
