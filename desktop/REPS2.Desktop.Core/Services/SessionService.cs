using System.Text.Json;
using REPS2.Desktop.Api.Contracts;

namespace REPS2.Desktop.Core.Services;

/// <summary>
/// Coordinates bounded RG REPS2 ENGINE probes. A probe is evidence collection,
/// never a boot or playability claim.
/// </summary>
public sealed class SessionService(IToolchainClient toolchain) : ISessionService
{
    public async Task<ProbeReport> RunBoundedProbeAsync(
        BoundedProbeRequest request, CancellationToken cancellationToken = default)
    {
        var game = request.Game;
        if (!game.CanRunProbe || string.IsNullOrWhiteSpace(game.NativeModulePath))
            throw new InvalidOperationException("A bounded probe is not available for this lifecycle state.");

        var ram = Path.Combine(game.BuildDirectory ?? string.Empty, "boot.ram.bin");
        if (!File.Exists(ram))
            throw new FileNotFoundException("The native build record does not have its required initial EE RAM image.", ram);

        var probe = await toolchain.RunProbeAsync(
            request.EngineRunnerPath,
            new RunProbeRequest(game.NativeModulePath, ram, game.SourceIsoPath, request.InstructionBudget),
            request.ReportDirectory,
            cancellationToken);

        if (probe.Invocation.ExitCode != 0)
            throw new ProbeExecutionException(
                $"The engine runner exited with code {probe.Invocation.ExitCode}.", probe);

        try
        {
            using var document = JsonDocument.Parse(probe.ReportText);
            var root = document.RootElement;
            var kind = root.TryGetProperty("kind", out var kindElement) ? kindElement.GetString() ?? string.Empty : string.Empty;
            if (kind != ContractVersions.EngineRunReportKind)
                throw new JsonException("Unexpected engine report kind.");

            var outcome = root.TryGetProperty("outcome", out var outcomeElement)
                ? outcomeElement.GetString() ?? "unknown" : "unknown";
            var session = root.TryGetProperty("session", out var sessionElement) ? sessionElement : default;
            var pc = session.ValueKind == JsonValueKind.Object
                && session.TryGetProperty("pc_hex", out var pcElement) ? pcElement.GetString() : null;
            var instructions = session.ValueKind == JsonValueKind.Object
                && session.TryGetProperty("instructions_executed", out var countElement)
                && countElement.TryGetInt64(out var count) ? count : 0;
            return new ProbeReport(kind, outcome, pc, instructions, probe.ReportPath);
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException("The runner did not return a valid run-report JSON document.", ex);
        }
    }
}

public sealed class ProbeExecutionException(string message, ProbeInvocationResult probe) : Exception(message)
{
    public ProbeInvocationResult Probe { get; } = probe;
}
