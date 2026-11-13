namespace REPS2.Desktop.Api.Contracts;

/// <summary>
/// Evidence-gated compatibility lifecycle. States stay distinct: a native
/// build or a bounded probe never promotes a title to Boot-tested or Playable.
/// </summary>
public enum GameLifecycle
{
    Imported,
    Analyzed,
    Translated,
    NativeBuilt,
    BootTested,
    Playable,
    AnalysisFailed,
    TranslationIncomplete,
    NativeBuildFailed,
    ExecutionBlocked,
    RecoveryRequired,
    Unknown
}

public static class GameLifecycleExtensions
{
    public static string Label(this GameLifecycle state) => state switch
    {
        GameLifecycle.Imported => "Imported",
        GameLifecycle.Analyzed => "Analyzed",
        GameLifecycle.Translated => "Translated",
        GameLifecycle.NativeBuilt => "Native-built",
        GameLifecycle.BootTested => "Boot-tested",
        GameLifecycle.Playable => "Playable",
        GameLifecycle.AnalysisFailed => "Analysis failed",
        GameLifecycle.TranslationIncomplete => "Translation incomplete",
        GameLifecycle.NativeBuildFailed => "Native build failed",
        GameLifecycle.ExecutionBlocked => "Execution blocked",
        GameLifecycle.RecoveryRequired => "Recovery required",
        _ => "Unknown"
    };

    public static string EvidenceNote(this GameLifecycle state) => state switch
    {
        GameLifecycle.Imported => "ISO and boot ELF identities are recorded.",
        GameLifecycle.Analyzed => "Analysis coverage is recorded for this boot ELF.",
        GameLifecycle.Translated => "Generated code and initial EE RAM are present.",
        GameLifecycle.NativeBuilt => "A native module and matching build identity are present.",
        GameLifecycle.BootTested => "A repeatable guest-visible boot stage is evidenced.",
        GameLifecycle.Playable => "A documented gameplay acceptance plan passed.",
        GameLifecycle.AnalysisFailed => "The frontend recorded an analysis failure.",
        GameLifecycle.TranslationIncomplete => "Translation artifacts are incomplete or unavailable.",
        GameLifecycle.NativeBuildFailed => "The native host build did not produce a valid module.",
        GameLifecycle.ExecutionBlocked => "The last execution report records a guest/runtime stop.",
        GameLifecycle.RecoveryRequired => "A source or required artifact is missing. Recovery is required.",
        _ => "Required evidence is missing or cannot be parsed."
    };

    public static string NextAction(this GameLifecycle state) => state switch
    {
        GameLifecycle.Imported => "Inspect analysis",
        GameLifecycle.Analyzed => "Prepare native build",
        GameLifecycle.Translated => "Build native module",
        GameLifecycle.NativeBuilt => "Run bounded probe",
        GameLifecycle.BootTested => "Review gameplay acceptance",
        GameLifecycle.Playable => "Play",
        GameLifecycle.RecoveryRequired => "Review source path",
        GameLifecycle.AnalysisFailed => "Review diagnostics",
        GameLifecycle.TranslationIncomplete => "Review unsupported coverage",
        GameLifecycle.NativeBuildFailed => "Review compiler diagnostics",
        GameLifecycle.ExecutionBlocked => "Inspect execution report",
        _ => "Inspect evidence"
    };
}
