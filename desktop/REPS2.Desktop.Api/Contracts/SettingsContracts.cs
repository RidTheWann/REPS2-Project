namespace REPS2.Desktop.Api.Contracts;

/// <summary>Persisted desktop settings. Values are local, offline-first, and user-scoped.</summary>
public sealed record DesktopSettings
{
    public string LibraryRoot { get; init; } = string.Empty;
    public string FrontendPath { get; init; } = string.Empty;
    public string EngineRunnerPath { get; init; } = string.Empty;
    public string SdkIncludeRoot { get; init; } = string.Empty;
    public string CMakePath { get; init; } = string.Empty;
    public bool DeveloperDiagnostics { get; init; } = true;
    public bool AutoCheckUpdates { get; init; } = true;
    public int DefaultProbeBudget { get; init; } = 5_000_000;
    public int TranslationUnitCount { get; init; } = 4;
}

/// <summary>Resolved locations of the external REPS2 tool processes and SDK.</summary>
public sealed record ToolchainResolution(
    string? FrontendPath,
    string? EngineRunnerPath,
    string? SdkIncludeRoot,
    string? CMakePath,
    IReadOnlyList<string> Notes)
{
    public bool CanImport => FrontendPath is not null;
    public bool CanBuild => FrontendPath is not null && SdkIncludeRoot is not null && CMakePath is not null;
}
