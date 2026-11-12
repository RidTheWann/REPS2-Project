namespace REPS2.Desktop.Core.Models;

public sealed record JobRecord
{
    public string Kind { get; init; } = "reps2.desktop.job.v1";
    public int SchemaVersion { get; init; } = 1;
    public required string JobId { get; init; }
    public required string GameId { get; init; }
    public required string GameName { get; init; }
    public required string JobType { get; init; }
    public required string Status { get; init; }
    public required string Stage { get; init; }
    public required DateTimeOffset CreatedAt { get; init; }
    public DateTimeOffset UpdatedAt { get; init; }
    public string UpdatedLabel => UpdatedAt.ToLocalTime().ToString("g");
    public string? Message { get; init; }
    public string? OutputDirectory { get; init; }
    public string? DiagnosticsDirectory { get; init; }
    public int? ExitCode { get; init; }
    public bool HasOutputDirectory => !string.IsNullOrWhiteSpace(OutputDirectory);
    public bool HasDiagnosticsDirectory => !string.IsNullOrWhiteSpace(DiagnosticsDirectory);
    public string ExitCodeLabel => ExitCode is { } code ? $"Exit code: {code}" : "Exit code: not reported";
}
