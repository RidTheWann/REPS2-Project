using REPS2.Desktop.Api.Contracts;

namespace REPS2.Desktop.Api.Ipc;

/// <summary>
/// Versioned IPC client for the external REPS2 Rust toolchain and RG REPS2
/// ENGINE runner. Each call is one bounded process invocation; results carry
/// the contract identity so callers can reject mismatched tool versions.
/// </summary>
public sealed class ProcessToolchainClient(string applicationRoot) : IToolchainClient
{
    public string ContractVersion => ContractVersions.Toolchain;

    public async Task<ToolInvocationResult> ImportGameAsync(
        string frontendPath, ImportGameRequest request, CancellationToken cancellationToken = default)
    {
        var arguments = new List<string>();
        if (request.DeveloperDiagnostics && !string.IsNullOrWhiteSpace(request.LogDirectory))
            arguments.AddRange(["--dev", "--log-dir", request.LogDirectory]);
        arguments.AddRange(["add-game", request.SourceIsoPath, "--library", request.LibraryRoot]);

        var result = await ProcessRunner.RunAsync(frontendPath, arguments,
            applicationRoot, cancellationToken, TimeSpan.FromHours(2));
        return ToInvocation(result);
    }

    public async Task<ToolInvocationResult> RecompileIsoAsync(
        string frontendPath, RecompileIsoRequest request, CancellationToken cancellationToken = default)
    {
        var arguments = new List<string>();
        if (request.DeveloperDiagnostics && !string.IsNullOrWhiteSpace(request.LogDirectory))
            arguments.AddRange(["--dev", "--log-dir", request.LogDirectory]);
        arguments.AddRange([
            "recompile-iso", request.SourceIsoPath, request.OutputRoot,
            "--discover", "--tu-count", Math.Clamp(request.TranslationUnitCount, 1, 16).ToString()
        ]);
        if (request.EnableHleSyscalls)
            arguments.Add("--hle-syscalls");

        var result = await ProcessRunner.RunAsync(frontendPath, arguments,
            applicationRoot, cancellationToken, TimeSpan.FromHours(2));
        return ToInvocation(result);
    }

    public async Task<ToolInvocationResult> RunDeveloperCommandAsync(
        string frontendPath, string command, string logDirectory, CancellationToken cancellationToken = default)
    {
        var arguments = new List<string> { "--log-dir", logDirectory, "dev" };
        arguments.AddRange(command.Split(' ', StringSplitOptions.RemoveEmptyEntries));
        var result = await ProcessRunner.RunAsync(frontendPath, arguments,
            applicationRoot, cancellationToken, TimeSpan.FromMinutes(2));
        return ToInvocation(result);
    }

    public async Task<ProbeInvocationResult> RunProbeAsync(
        string engineRunnerPath, RunProbeRequest request, string reportDirectory, CancellationToken cancellationToken = default)
    {
        var result = await ProcessRunner.RunAsync(engineRunnerPath,
            [
                request.NativeModulePath, "--ram", request.RamImagePath, "--media", request.SourceIsoPath,
                "--budget", Math.Clamp(request.InstructionBudget, 1, 100_000_000).ToString(),
                "--max-batches", "1", "--hle-syscalls", "--instruction-trace"
            ],
            applicationRoot, cancellationToken, TimeSpan.FromMinutes(10));

        var reportText = result.StandardOutput.Trim();
        if (reportText.Length == 0)
            reportText = result.CombinedOutput;
        Directory.CreateDirectory(reportDirectory);
        var reportName = $"probe-{DateTimeOffset.UtcNow:yyyyMMddHHmmss}.json";
        var reportPath = Path.Combine(reportDirectory, reportName);
        await File.WriteAllTextAsync(reportPath, reportText, cancellationToken);
        return new ProbeInvocationResult(reportPath, reportText, ToInvocation(result));
    }

    private ToolInvocationResult ToInvocation(ProcessResult result) =>
        new(ContractVersion, result.ExitCode, result.StandardOutput, result.StandardError, result.Duration);
}
