using REPS2.Desktop.Core.Models;
using REPS2.Desktop.Core.Services;
using Xunit;

namespace REPS2.Desktop.Core.Tests;

public sealed class CompatibilityStateDeriverTests
{
    [Fact]
    public void ImportedManifestDoesNotBecomePlayable()
    {
        using var fixture = new ManifestFixture(analysisStatus: "pending", withTranslation: false);
        var game = CompatibilityStateDeriver.Parse(fixture.ManifestPath);
        Assert.Equal(GameLifecycle.Imported, game.Lifecycle);
        Assert.False(game.CanPlay);
        Assert.False(game.CanRunProbe);
    }

    [Fact]
    public void AnalysisEvidenceWithoutTranslationArtifactsIsIncomplete()
    {
        using var fixture = new ManifestFixture(analysisStatus: "completed", withTranslation: false);
        var game = CompatibilityStateDeriver.Parse(fixture.ManifestPath);
        Assert.Equal(GameLifecycle.TranslationIncomplete, game.Lifecycle);
        Assert.False(game.CanPlay);
        Assert.False(game.CanBuild);
    }

    [Fact]
    public void TranslationRequiresBothRecordedArtifactsAndExactRamSize()
    {
        using var fixture = new ManifestFixture(analysisStatus: "completed", withTranslation: true);
        var game = CompatibilityStateDeriver.Parse(fixture.ManifestPath);
        Assert.Equal(GameLifecycle.Translated, game.Lifecycle);
        Assert.True(game.CanBuild);
        Assert.False(game.CanPlay);
    }

    [Fact]
    public void GeneratedFilesCannotSkipFailedAnalysis()
    {
        using var fixture = new ManifestFixture(analysisStatus: "failed", withTranslation: true);
        var game = CompatibilityStateDeriver.Parse(fixture.ManifestPath);
        Assert.Equal(GameLifecycle.AnalysisFailed, game.Lifecycle);
        Assert.False(game.CanBuild);
    }

    [Fact]
    public void MissingSourceIsoRequiresRecovery()
    {
        using var fixture = new ManifestFixture(analysisStatus: "completed", withTranslation: true);
        File.Delete(fixture.IsoPath);
        var game = CompatibilityStateDeriver.Parse(fixture.ManifestPath);
        Assert.Equal(GameLifecycle.RecoveryRequired, game.Lifecycle);
        Assert.Contains("original ISO", game.RecoveryReason);
    }

    [Fact]
    public void MissingBootElfRequiresRecovery()
    {
        using var fixture = new ManifestFixture(analysisStatus: "completed", withTranslation: true);
        File.Delete(Path.Combine(fixture.GameDirectory, "boot.elf"));
        var game = CompatibilityStateDeriver.Parse(fixture.ManifestPath);
        Assert.Equal(GameLifecycle.RecoveryRequired, game.Lifecycle);
    }

    [Fact]
    public void ModifiedBootElfRequiresRecovery()
    {
        using var fixture = new ManifestFixture(analysisStatus: "completed", withTranslation: true);
        File.WriteAllBytes(Path.Combine(fixture.GameDirectory, "boot.elf"), [0x7f, 0x45, 0x4c, 0x46, 0x00]);
        var game = CompatibilityStateDeriver.Parse(fixture.ManifestPath);
        Assert.Equal(GameLifecycle.RecoveryRequired, game.Lifecycle);
        Assert.Contains("boot ELF", game.RecoveryReason);
    }

    [Fact]
    public void BuildIdentityCannotPromoteAnUntranslatedGame()
    {
        using var fixture = new ManifestFixture(analysisStatus: "completed", withTranslation: false);
        var build = fixture.CreateBuildRecord();
        var game = CompatibilityStateDeriver.Parse(fixture.ManifestPath, build.RecordPath);
        Assert.Equal(GameLifecycle.TranslationIncomplete, game.Lifecycle);
    }

    [Fact]
    public void SnakeCaseNativeBuildRecordPromotesTranslatedGameToNativeBuilt()
    {
        using var fixture = new ManifestFixture(analysisStatus: "completed", withTranslation: true);
        var build = fixture.CreateBuildRecord();
        var serialized = File.ReadAllText(build.RecordPath);
        Assert.Contains("\"source_iso_sha256\"", serialized);
        Assert.Contains("\"native_module_path\"", serialized);

        var game = CompatibilityStateDeriver.Parse(fixture.ManifestPath, build.RecordPath);
        Assert.Equal(GameLifecycle.NativeBuilt, game.Lifecycle);
        Assert.True(game.CanRunProbe);
        Assert.False(game.CanPlay);
        Assert.Equal(build.ModulePath, game.NativeModulePath);
    }

    private sealed class ManifestFixture : IDisposable
    {
        private const string GameId = "sha256-" + "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

        public ManifestFixture(string analysisStatus, bool withTranslation)
        {
            Root = Path.Combine(Path.GetTempPath(), "reps2-tests-" + Guid.NewGuid().ToString("N"));
            GameDirectory = Path.Combine(Root, "games", GameId);
            Directory.CreateDirectory(GameDirectory);
            IsoPath = Path.Combine(Root, "owned.iso");
            File.WriteAllBytes(IsoPath, [0x00, 0x01, 0x02, 0x03]);
            BootElfBytes = [0x7f, 0x45, 0x4c, 0x46];
            File.WriteAllBytes(Path.Combine(GameDirectory, "boot.elf"), BootElfBytes);
            if (withTranslation)
            {
                File.WriteAllText(Path.Combine(GameDirectory, "recompiled.cpp"), "// fixture");
                using var ram = File.Create(Path.Combine(GameDirectory, "ee-main-ram.bin"));
                ram.SetLength(32L * 1024L * 1024L);
            }

            ManifestPath = Path.Combine(GameDirectory, "game.json");
            var generated = withTranslation ? "true" : "false";
            var isoSha = HashFile(IsoPath);
            var elfSha = HashBytes(BootElfBytes);
            File.WriteAllText(ManifestPath, $$"""
            {
              "schema_version": 1,
              "game_id": "{{GameId}}",
              "display_name": "Fixture PS2 Game",
              "serial_candidate": "SLUS_123.45",
              "source_iso": { "path": {{System.Text.Json.JsonSerializer.Serialize(IsoPath)}}, "sha256": "{{isoSha}}", "size_bytes": 4 },
              "disc": { "volume_identifier": "FIXTURE" },
              "boot": { "sha256": "{{elfSha}}", "elf": { "entry_point": "0x00100008" } },
              "analysis": { "status": "{{analysisStatus}}", "basic_block_count": 12, "decoded_instruction_count": 64 },
              "native_build": { "generated_cpp": {{generated}}, "generated_ram_image": {{generated}}, "playable": false },
              "artifacts": { "boot_elf": "boot.elf", "generated_cpp": "recompiled.cpp", "initial_ee_ram": "ee-main-ram.bin" }
            }
            """);
        }

        public string Root { get; }
        public string GameDirectory { get; }
        public string IsoPath { get; }
        public string ManifestPath { get; }
        private byte[] BootElfBytes { get; }

        public (string RecordPath, string ModulePath) CreateBuildRecord()
        {
            var buildDirectory = Path.Combine(Root, "build");
            Directory.CreateDirectory(buildDirectory);
            var modulePath = Path.Combine(buildDirectory, "module.dll");
            var identityPath = Path.Combine(buildDirectory, "identity.json");
            File.WriteAllBytes(modulePath, [0x4d, 0x5a]);
            var key = new string('c', 64);
            File.WriteAllText(identityPath, $$"""{"cache_key_sha256":"{{key}}"}""");
            var recordPath = Path.Combine(Root, "build-record.json");
            var record = new NativeBuildRecord
            {
                GameId = GameId,
                SourceIsoSha256 = HashFile(IsoPath),
                BuildDirectory = buildDirectory,
                ManifestPath = ManifestPath,
                NativeModulePath = modulePath,
                BuildIdentityPath = identityPath,
                CacheKeySha256 = key,
                CompletedAt = DateTimeOffset.UtcNow
            };
            File.WriteAllText(recordPath, System.Text.Json.JsonSerializer.Serialize(record,
                new System.Text.Json.JsonSerializerOptions(System.Text.Json.JsonSerializerDefaults.Web)));
            return (recordPath, modulePath);
        }

        private static string HashFile(string path)
        {
            using var stream = File.OpenRead(path);
            return Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(stream)).ToLowerInvariant();
        }

        private static string HashBytes(byte[] bytes) =>
            Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes)).ToLowerInvariant();

        public void Dispose()
        {
            try { Directory.Delete(Root, recursive: true); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }
}
