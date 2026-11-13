using REPS2.Desktop.Api.Contracts;

namespace REPS2.Desktop.Core.Services;

/// <summary>Runs versioned developer reports exposed by the Rust frontend.</summary>
public sealed class DiagnosticsService(IToolchainClient toolchain) : IDiagnosticsService
{
    public Task<ToolInvocationResult> RunCommandAsync(
        string frontendPath, string command, string logDirectory, CancellationToken cancellationToken = default) =>
        toolchain.RunDeveloperCommandAsync(frontendPath, command, logDirectory, cancellationToken);
}
