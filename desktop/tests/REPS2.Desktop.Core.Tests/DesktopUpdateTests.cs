using System.IO.Compression;
using REPS2.Desktop.Core.Services;
using Xunit;

namespace REPS2.Desktop.Core.Tests;

public sealed class DesktopUpdateTests
{
    [Fact]
    public void SemanticVersionOrdersAlphaAndStableVersions()
    {
        Assert.True(DesktopSemanticVersion.TryParse("0.1.0-alpha.2", out var alpha2));
        Assert.True(DesktopSemanticVersion.TryParse("0.1.0-alpha.10", out var alpha10));
        Assert.True(DesktopSemanticVersion.TryParse("0.1.0", out var stable));
        Assert.True(DesktopSemanticVersion.TryParse("0.2.0-alpha.1", out var nextMinor));
        Assert.True(alpha2.CompareTo(alpha10) < 0);
        Assert.True(alpha10.CompareTo(stable) < 0);
        Assert.True(stable.CompareTo(nextMinor) < 0);
    }

    [Theory]
    [InlineData("0.1.0-alpha.1", true)]
    [InlineData("1.2.3+build.42", true)]
    [InlineData("v1.2.3", false)]
    [InlineData("1.2", false)]
    [InlineData("01.2.3", false)]
    public void SemanticVersionParserAcceptsOnlyThreePartVersions(string value, bool expected)
    {
        Assert.Equal(expected, DesktopSemanticVersion.TryParse(value, out _));
    }

    [Fact]
    public async Task SafeExtractorRestoresOrdinaryApplicationFiles()
    {
        using var fixture = new ZipFixture();
        fixture.AddFile("REPS2.Desktop.exe", [0x4d, 0x5a]);
        fixture.AddFile("tools/bin/reps2.exe", [0x4d, 0x5a]);
        fixture.AddFile("nested/config.json", [0x7b, 0x7d]);

        await UpdatePackageApplier.ExtractSafelyAsync(fixture.ArchivePath, fixture.Destination);
        Assert.Equal(new byte[] { 0x4d, 0x5a }, await File.ReadAllBytesAsync(Path.Combine(fixture.Destination, "REPS2.Desktop.exe")));
        Assert.Equal(new byte[] { 0x7b, 0x7d }, await File.ReadAllBytesAsync(Path.Combine(fixture.Destination, "nested", "config.json")));
    }

    [Fact]
    public async Task SafeExtractorRejectsZipSlipTraversal()
    {
        using var fixture = new ZipFixture();
        fixture.AddFile("../outside.txt", [0x78]);
        await Assert.ThrowsAsync<InvalidDataException>(() =>
            UpdatePackageApplier.ExtractSafelyAsync(fixture.ArchivePath, fixture.Destination));
        Assert.False(File.Exists(Path.Combine(fixture.Root, "outside.txt")));
    }

    [Fact]
    public async Task SafeExtractorRejectsAbsoluteAndDriveQualifiedPaths()
    {
        using var absolute = new ZipFixture();
        absolute.AddFile("/outside.txt", [0x78]);
        await Assert.ThrowsAsync<InvalidDataException>(() =>
            UpdatePackageApplier.ExtractSafelyAsync(absolute.ArchivePath, absolute.Destination));

        using var drive = new ZipFixture();
        drive.AddFile("C:/outside.txt", [0x78]);
        await Assert.ThrowsAsync<InvalidDataException>(() =>
            UpdatePackageApplier.ExtractSafelyAsync(drive.ArchivePath, drive.Destination));
    }

    [Fact]
    public async Task SafeExtractorRejectsCaseInsensitiveDuplicatePaths()
    {
        using var fixture = new ZipFixture();
        fixture.AddFile("REPS2.Desktop.exe", [0x4d]);
        fixture.AddFile("reps2.desktop.exe", [0x5a]);
        await Assert.ThrowsAsync<InvalidDataException>(() =>
            UpdatePackageApplier.ExtractSafelyAsync(fixture.ArchivePath, fixture.Destination));
    }

    [Fact]
    public async Task SafeExtractorRejectsSymbolicLinks()
    {
        using var fixture = new ZipFixture();
        fixture.AddSymbolicLink("link", "../outside.txt");
        await Assert.ThrowsAsync<InvalidDataException>(() =>
            UpdatePackageApplier.ExtractSafelyAsync(fixture.ArchivePath, fixture.Destination));
    }

    private sealed class ZipFixture : IDisposable
    {
        public ZipFixture()
        {
            Root = Path.Combine(Path.GetTempPath(), "reps2-update-test-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Root);
            ArchivePath = Path.Combine(Root, "update.zip");
            Destination = Path.Combine(Root, "stage");
        }

        public string Root { get; }
        public string ArchivePath { get; }
        public string Destination { get; }

        public void AddFile(string name, byte[] contents)
        {
            using var archive = ZipFile.Open(ArchivePath, File.Exists(ArchivePath) ? ZipArchiveMode.Update : ZipArchiveMode.Create);
            var entry = archive.CreateEntry(name);
            using var stream = entry.Open();
            stream.Write(contents);
        }

        public void AddSymbolicLink(string name, string target)
        {
            using var archive = ZipFile.Open(ArchivePath, File.Exists(ArchivePath) ? ZipArchiveMode.Update : ZipArchiveMode.Create);
            var entry = archive.CreateEntry(name);
            entry.ExternalAttributes = unchecked((int)0xA1FF0000);
            using var stream = new StreamWriter(entry.Open());
            stream.Write(target);
        }

        public void Dispose()
        {
            try { Directory.Delete(Root, recursive: true); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }
}
