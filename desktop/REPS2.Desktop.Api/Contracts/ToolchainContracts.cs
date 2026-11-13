namespace REPS2.Desktop.Api.Contracts;

/// <summary>Outcome of one external tool invocation, with its contract version.</summary>
public sealed record ToolInvocationResult(
    string ContractVersion,
    int ExitCode,
    string StandardOutput,
    string StandardError,
    TimeSpan Duration)
{
    public string CombinedOutput => string.Join(Environment.NewLine,
        new[] { StandardOutput.Trim(), StandardError.Trim() }.Where(value => value.Length > 0));
}

/// <summary>Request for a raw-ISO import into the game library.</summary>
public sealed record ImportGameRequest(
    string SourceIsoPath,
    string LibraryRoot,
    bool DeveloperDiagnostics = false,
    string? LogDirectory = null);

/// <summary>Request for ISO analysis and native-project generation.</summary>
public sealed record RecompileIsoRequest(
    string SourceIsoPath,
    string OutputRoot,
    int TranslationUnitCount,
    bool EnableHleSyscalls = true,
    bool DeveloperDiagnostics = false,
    string? LogDirectory = null);

/// <summary>Request for a bounded RG REPS2 ENGINE probe of one native module.</summary>
public sealed record RunProbeRequest(
    string NativeModulePath,
    string RamImagePath,
    string SourceIsoPath,
    int InstructionBudget);

/// <summary>Raw engine-runner output plus the persisted report location.</summary>
public sealed record ProbeInvocationResult(string ReportPath, string ReportText, ToolInvocationResult Invocation);

/// <summary>Parsed session summary from an engine run report.</summary>
public sealed record ProbeReport(
    string Kind,
    string Outcome,
    string? ProgramCounterHex,
    long InstructionsExecuted,
    string ReportPath);

/// <summary>
/// IPC contract to the external REPS2 Rust toolchain and engine runner.
/// Implementations invoke the versioned CLI processes; callers never guess
/// internal implementation details or scrape human-oriented messages.
/// </summary>
public interface IToolchainClient
{
    string ContractVersion { get; }

    Task<ToolInvocationResult> ImportGameAsync(
        string frontendPath, ImportGameRequest request, CancellationToken cancellationToken = default);

    Task<ToolInvocationResult> RecompileIsoAsync(
        string frontendPath, RecompileIsoRequest request, CancellationToken cancellationToken = default);

    Task<ToolInvocationResult> RunDeveloperCommandAsync(
        string frontendPath, string command, string logDirectory, CancellationToken cancellationToken = default);

    Task<ProbeInvocationResult> RunProbeAsync(
        string engineRunnerPath, RunProbeRequest request, string reportDirectory, CancellationToken cancellationToken = default);
}
