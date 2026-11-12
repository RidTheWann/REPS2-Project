using System.Security.Cryptography;
using System.Text.Json.Nodes;
using REPS2.Desktop.Core.Models;
using REPS2.Desktop.Core.Services;
using Xunit;

namespace REPS2.Desktop.Core.Tests;

public sealed class SourceIsoRelinkServiceTests
{
    [Fact]
    public async Task RelinkUpdatesOnlyTheSourcePathAfterExactDigestMatch()
    {
        using var fixture = new RelinkFixture();
        var replacement = fixture.CreateReplacement("relocated.iso", fixture.OriginalBytes);

        await SourceIsoRelinkService.RelinkAsync(fixture.LibraryRoot, fixture.Game, replacement);

        var manifest = JsonNode.Parse(await File.ReadAllTextAsync(fixture.ManifestPath))!;
        Assert.Equal(Path.GetFullPath(replacement), manifest["source_iso"]?["path"]?.GetValue<string>());
        Assert.Equal(fixture.Sha256, manifest["source_iso"]?["sha256"]?.GetValue<string>());
        Assert.Equal("Fixture PS2 Game", manifest["display_name"]?.GetValue<string>());
        Assert.Equal("0x00100008", manifest["boot"]?["elf"]?["entry_point"]?.GetValue<string>());
    }

    [Fact]
    public async Task RelinkRejectsDifferentIsoBytesAndLeavesManifestUntouched()
    {
        using var fixture = new RelinkFixture();
        var replacement = fixture.CreateReplacement("different.iso", [0x10, 0x11, 0x12, 0x13, 0x14, 0x15]);
        var before = await File.ReadAllTextAsync(fixture.ManifestPath);

        var error = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            SourceIsoRelinkService.RelinkAsync(fixture.LibraryRoot, fixture.Game, replacement));

        Assert.Contains("SHA-256 mismatch", error.Message);
        Assert.Equal(before, await File.ReadAllTextAsync(fixture.ManifestPath));
    }

    [Fact]
    public async Task RelinkRejectsWrongSizeBeforeChangingManifest()
    {
        using var fixture = new RelinkFixture();
        var replacement = fixture.CreateReplacement("wrong-size.iso", [0x01, 0x02]);
        var before = await File.ReadAllTextAsync(fixture.ManifestPath);

        var error = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            SourceIsoRelinkService.RelinkAsync(fixture.LibraryRoot, fixture.Game, replacement));

        Assert.Contains("file size", error.Message);
        Assert.Equal(before, await File.ReadAllTextAsync(fixture.ManifestPath));
    }

    [Fact]
    public async Task RelinkRejectsMissingRecordedSizeAndLeavesManifestUntouched()
    {
        using var fixture = new RelinkFixture();
        var replacement = fixture.CreateReplacement("relocated.iso", fixture.OriginalBytes);
        var manifest = JsonNode.Parse(await File.ReadAllTextAsync(fixture.ManifestPath))!.AsObject();
        manifest["source_iso"]!.AsObject().Remove("size_bytes");
        await File.WriteAllTextAsync(fixture.ManifestPath, manifest.ToJsonString());
        var before = await File.ReadAllTextAsync(fixture.ManifestPath);

        await Assert.ThrowsAsync<InvalidDataException>(() =>
            SourceIsoRelinkService.RelinkAsync(fixture.LibraryRoot, fixture.Game, replacement));

        Assert.Equal(before, await File.ReadAllTextAsync(fixture.ManifestPath));
    }

    [Fact]
    public async Task RelinkRejectsGameIdTraversalBeforeReadingOutsideLibrary()
    {
        using var fixture = new RelinkFixture();
        var invalid = fixture.Game with { GameId = "../outside" };
        var replacement = fixture.CreateReplacement("relocated.iso", fixture.OriginalBytes);

        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            SourceIsoRelinkService.RelinkAsync(fixture.LibraryRoot, invalid, replacement));
    }

    private sealed class RelinkFixture : IDisposable
    {
        public RelinkFixture()
        {
            Root = Path.Combine(Path.GetTempPath(), "reps2-relink-" + Guid.NewGuid().ToString("N"));
            LibraryRoot = Path.Combine(Root, "Library");
            OriginalBytes = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05];
            Sha256 = Convert.ToHexString(SHA256.HashData(OriginalBytes)).ToLowerInvariant();
            GameId = $"sha256-{Sha256}";
            GameDirectory = Path.Combine(LibraryRoot, "games", GameId);
            Directory.CreateDirectory(GameDirectory);
            ManifestPath = Path.Combine(GameDirectory, "game.json");
            File.WriteAllText(ManifestPath, $$"""
            {
              "schema_version": 1,
              "game_id": "{{GameId}}",
              "display_name": "Fixture PS2 Game",
              "source_iso": {
                "path": "Z:/old-location/original.iso",
                "sha256": "{{Sha256}}",
                "size_bytes": {{OriginalBytes.Length}}
              },
              "boot": { "elf": { "entry_point": "0x00100008" } }
            }
            """);
            Game = new GameSummary
            {
                GameId = GameId,
                DisplayName = "Fixture PS2 Game",
                IsoSha256 = Sha256,
                SourceIsoPath = "Z:/old-location/original.iso",
                Lifecycle = GameLifecycle.RecoveryRequired,
                RecoveryReason = "The original ISO is not available at the recorded path."
            };
        }

        public string Root { get; }
        public string LibraryRoot { get; }
        public string GameId { get; }
        public string GameDirectory { get; }
        public string ManifestPath { get; }
        public byte[] OriginalBytes { get; }
        public string Sha256 { get; }
        public GameSummary Game { get; }

        public string CreateReplacement(string name, byte[] bytes)
        {
            var path = Path.Combine(Root, name);
            File.WriteAllBytes(path, bytes);
            return path;
        }

        public void Dispose()
        {
            try { Directory.Delete(Root, recursive: true); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }
}
