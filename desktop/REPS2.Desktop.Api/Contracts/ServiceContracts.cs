namespace REPS2.Desktop.Api.Contracts;

/// <summary>Application-level contracts implemented by the service layer.</summary>
public interface IGameLibraryService
{
    Task<LibrarySnapshot> ScanAsync(string libraryRoot, CancellationToken cancellationToken = default);
}

public interface IBuildService
{
    Task<NativeBuildRecord> BuildAsync(
        GameSummary game,
        CancellationToken cancellationToken = default,
        Func<JobRecord, Task>? onProgress = null);
}

public interface ISettingsService
{
    Task<DesktopSettings> LoadAsync(CancellationToken cancellationToken = default);
    Task SaveAsync(DesktopSettings settings, CancellationToken cancellationToken = default);
}

public interface ISessionService
{
    Task<ProbeReport> RunBoundedProbeAsync(BoundedProbeRequest request, CancellationToken cancellationToken = default);
}

/// <summary>Input for a bounded execution probe; mirrors the engine runner contract.</summary>
public sealed record BoundedProbeRequest(
    GameSummary Game,
    string EngineRunnerPath,
    string ApplicationRoot,
    string ReportDirectory,
    int InstructionBudget);

public interface IDiagnosticsService
{
    Task<ToolInvocationResult> RunCommandAsync(
        string frontendPath, string command, string logDirectory, CancellationToken cancellationToken = default);
}

/// <summary>Host-shell operations the presentation layer may request without spawning processes itself.</summary>
public interface IHostShellService
{
    void OpenDirectory(string directoryPath);
}
