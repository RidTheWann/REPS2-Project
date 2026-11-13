using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.Core.Services;

namespace REPS2.Desktop;

// Composition root for the presentation layer. Services are injected as
// their API-layer contracts; the UI never constructs backend implementations.
public sealed class DesktopAppContext(
    DesktopPaths paths,
    DesktopSettings settings,
    ISettingsService settingsStore,
    IGameLibraryService libraryService,
    IBuildService buildService,
    ISessionService sessionService,
    IDiagnosticsService diagnosticsService,
    JobStore jobStore,
    IToolchainClient toolchain,
    IHostShellService hostShell)
{
    public DesktopPaths Paths { get; } = paths;
    public DesktopSettings Settings { get; set; } = settings;
    public ISettingsService SettingsStore { get; } = settingsStore;
    public IGameLibraryService LibraryService { get; } = libraryService;
    public IBuildService BuildService { get; } = buildService;
    public ISessionService SessionService { get; } = sessionService;
    public IDiagnosticsService DiagnosticsService { get; } = diagnosticsService;
    public JobStore JobStore { get; } = jobStore;
    public IToolchainClient Toolchain { get; } = toolchain;
    public IHostShellService HostShell { get; } = hostShell;
    public Func<Task> AddGameAsync { get; set; } = () => Task.CompletedTask;
    public Func<GameSummary, Task> BuildAsync { get; set; } = _ => Task.CompletedTask;
    public Func<GameSummary, Task> RelinkSourceIsoAsync { get; set; } = _ => Task.CompletedTask;
    public Func<GameSummary, Task> RunProbeAsync { get; set; } = _ => Task.CompletedTask;
    public Action<GameSummary> OpenGame { get; set; } = _ => { };
    public Func<string, Task> RunDeveloperCommandAsync { get; set; } = _ => Task.CompletedTask;
    public Action<string> SetStatus { get; set; } = _ => { };
    public Action<DesktopSettings>? SettingsChanged { get; set; }
    public event Action<JobRecord>? JobUpdated;

    public void PublishJobUpdate(JobRecord job) => JobUpdated?.Invoke(job);
}

public static class AppServices
{
    public static DesktopAppContext Current { get; set; } = null!;
}
