namespace REPS2.Desktop.Api.Contracts;

/// <summary>
/// Versioned contract identities exchanged between the desktop presentation,
/// the service layer, and the external REPS2 tool processes.
/// </summary>
public static class ContractVersions
{
    /// <summary>Versioned request/response contract for the REPS2 Rust toolchain.</summary>
    public const string Toolchain = "reps2.toolchain.api/1";

    /// <summary>Versioned request/response contract for the RG REPS2 ENGINE runner.</summary>
    public const string EngineRunner = "reps2.engine.runner.api/1";

    /// <summary>Run-report document kind emitted by the engine runner.</summary>
    public const string EngineRunReportKind = "rg.reps2.engine.run_report.v1";

    /// <summary>Persisted desktop job record kind.</summary>
    public const string JobKind = "reps2.desktop.job.v1";

    /// <summary>Persisted native-build record kind.</summary>
    public const string NativeBuildKind = "reps2.desktop.native_build.v1";

    /// <summary>Current schema version for the persisted desktop records above.</summary>
    public const int DesktopSchemaVersion = 1;
}
