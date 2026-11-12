using REPS2.Desktop.Core.Models;
using REPS2.Desktop.Core.Services;

namespace REPS2.Desktop;

public sealed class DesktopAppContext(
    DesktopPaths paths,
    DesktopSettings settings,
    SettingsStore settingsStore,
    LibraryService libraryService,
    JobStore jobStore)
{
    public DesktopPaths Paths { get; } = paths;
    public DesktopSettings Settings { get; set; } = settings;
    public SettingsStore SettingsStore { get; } = settingsStore;
    public LibraryService LibraryService { get; } = libraryService;
    public JobStore JobStore { get; } = jobStore;
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
