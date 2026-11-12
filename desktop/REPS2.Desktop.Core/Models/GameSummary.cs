using System.Globalization;
using System.Text.Json.Serialization;

namespace REPS2.Desktop.Core.Models;

// A WinUI DataTemplate x:DataType is materialized by generated XamlTypeInfo code.
// Keep this presentation DTO parameterless and publicly settable so the XAML
// compiler can construct it; manifest validation stays in CompatibilityStateDeriver.
public sealed record GameSummary
{
    public string GameId { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public string? SerialCandidate { get; set; }
    public string GameDirectory { get; set; } = string.Empty;
    public string SourceIsoPath { get; set; } = string.Empty;
    public string IsoSha256 { get; set; } = string.Empty;
    public string BootElfPath { get; set; } = string.Empty;
    public string? BootElfSha256 { get; set; }
    public string? VolumeIdentifier { get; set; }
    public string? BootEntryPoint { get; set; }
    public GameLifecycle Lifecycle { get; set; } = GameLifecycle.Unknown;
    public string LifecycleLabel => Lifecycle.Label();
    public string EvidenceNote => Lifecycle.EvidenceNote();
    public string NextAction => Lifecycle.NextAction();
    public string? BuildDirectory { get; set; }
    public string? NativeModulePath { get; set; }
    public string? BuildIdentityPath { get; set; }
    public string? CacheKeySha256 { get; set; }
    public string? RecoveryReason { get; set; }
    public string? AnalysisError { get; set; }
    public long BasicBlockCount { get; set; }
    public long DecodedInstructionCount { get; set; }
    public long UnsupportedBlockCount { get; set; }
    public long UnresolvedIndirectCount { get; set; }
    public bool CanBuild => Lifecycle is GameLifecycle.Translated or GameLifecycle.NativeBuildFailed;
    public bool CanRunProbe => Lifecycle is GameLifecycle.NativeBuilt or GameLifecycle.ExecutionBlocked;
    public bool CanPlay => Lifecycle is GameLifecycle.Playable;
    public bool CanRelinkSourceIso => Lifecycle == GameLifecycle.RecoveryRequired
        && (RecoveryReason?.Contains("original ISO", StringComparison.OrdinalIgnoreCase) == true
            || RecoveryReason?.Contains("source ISO file size", StringComparison.OrdinalIgnoreCase) == true);
    public string Subtitle => string.IsNullOrWhiteSpace(SerialCandidate)
        ? (VolumeIdentifier ?? "PlayStation 2 disc")
        : SerialCandidate!;
    public string CoverageSummary => $"{BasicBlockCount.ToString("N0", CultureInfo.InvariantCulture)} blocks · {DecodedInstructionCount.ToString("N0", CultureInfo.InvariantCulture)} instructions";
}

public sealed record LibrarySnapshot(
    IReadOnlyList<GameSummary> Games,
    IReadOnlyList<string> Warnings,
    string LibraryRoot);

public sealed record NativeBuildRecord
{
    [JsonPropertyName("kind")]
    public string Kind { get; init; } = "reps2.desktop.native_build.v1";
    [JsonPropertyName("schema_version")]
    public int SchemaVersion { get; init; } = 1;
    [JsonPropertyName("game_id")]
    public required string GameId { get; init; }
    [JsonPropertyName("source_iso_sha256")]
    public required string SourceIsoSha256 { get; init; }
    [JsonPropertyName("build_directory")]
    public required string BuildDirectory { get; init; }
    [JsonPropertyName("manifest_path")]
    public required string ManifestPath { get; init; }
    [JsonPropertyName("native_module_path")]
    public required string NativeModulePath { get; init; }
    [JsonPropertyName("build_identity_path")]
    public required string BuildIdentityPath { get; init; }
    [JsonPropertyName("cache_key_sha256")]
    public required string CacheKeySha256 { get; init; }
    [JsonPropertyName("completed_at")]
    public DateTimeOffset CompletedAt { get; init; }
}
