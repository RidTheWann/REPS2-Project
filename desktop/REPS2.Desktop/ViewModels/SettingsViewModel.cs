using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.Core.Services;

namespace REPS2.Desktop.ViewModels;

public sealed class SettingsViewModel(ISettingsService settingsService) : ViewModelBase
{
    private string _status = "Settings are stored locally.";

    public string Status { get => _status; private set => Set(ref _status, value); }

    public DesktopSettings Compose(
        DesktopSettings current, string defaultLibraryRoot, string libraryRoot, string frontendPath,
        string runnerPath, string sdkIncludeRoot, string cmakePath, bool developerDiagnostics,
        bool autoCheckUpdates, int translationUnitCount, int probeBudget) =>
        current with
        {
            LibraryRoot = string.IsNullOrWhiteSpace(libraryRoot) ? defaultLibraryRoot : Path.GetFullPath(libraryRoot.Trim()),
            FrontendPath = frontendPath.Trim(),
            EngineRunnerPath = runnerPath.Trim(),
            SdkIncludeRoot = sdkIncludeRoot.Trim(),
            CMakePath = cmakePath.Trim(),
            DeveloperDiagnostics = developerDiagnostics,
            AutoCheckUpdates = autoCheckUpdates,
            TranslationUnitCount = translationUnitCount,
            DefaultProbeBudget = probeBudget
        };

    public async Task<DesktopSettings?> SaveAsync(DesktopSettings settings, DesktopPaths paths)
    {
        try
        {
            await settingsService.SaveAsync(settings);
            var resolution = ToolchainLocator.Resolve(settings, paths);
            Status = resolution.CanBuild
                ? "Settings saved · frontend, SDK headers and CMake are available. Confirm MSVC Build Tools before compiling."
                : "Settings saved · " + string.Join(" ", resolution.Notes);
            return settings;
        }
        catch (Exception ex)
        {
            Status = $"Settings could not be saved: {ex.Message}";
            return null;
        }
    }
}
