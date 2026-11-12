using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using REPS2.Desktop.Core.Services;

namespace REPS2.Desktop.Pages;

public sealed partial class SettingsPage : Page
{
    public SettingsPage()
    {
        InitializeComponent();
        Loaded += SettingsPage_Loaded;
    }

    private void SettingsPage_Loaded(object sender, RoutedEventArgs e) => LoadSettings();

    private void LoadSettings()
    {
        var settings = AppServices.Current.Settings;
        LibraryRootBox.Text = settings.LibraryRoot;
        FrontendPathBox.Text = settings.FrontendPath;
        RunnerPathBox.Text = settings.EngineRunnerPath;
        SdkIncludeRootBox.Text = settings.SdkIncludeRoot;
        CMakePathBox.Text = settings.CMakePath;
        DiagnosticsCheckBox.IsChecked = settings.DeveloperDiagnostics;
        AutoCheckUpdatesCheckBox.IsChecked = settings.AutoCheckUpdates;
        SelectCombo(TuCountBox, settings.TranslationUnitCount.ToString());
        SelectCombo(ProbeBudgetBox, settings.DefaultProbeBudget.ToString());
    }

    private static void SelectCombo(ComboBox combo, string tag)
    {
        combo.SelectedItem = combo.Items.OfType<ComboBoxItem>()
            .FirstOrDefault(item => Equals(item.Tag?.ToString(), tag));
    }

    private async void Save_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var current = AppServices.Current.Settings;
            var settings = current with
            {
                LibraryRoot = string.IsNullOrWhiteSpace(LibraryRootBox.Text)
                    ? AppServices.Current.Paths.Library : Path.GetFullPath(LibraryRootBox.Text.Trim()),
                FrontendPath = FrontendPathBox.Text.Trim(),
                EngineRunnerPath = RunnerPathBox.Text.Trim(),
                SdkIncludeRoot = SdkIncludeRootBox.Text.Trim(),
                CMakePath = CMakePathBox.Text.Trim(),
                DeveloperDiagnostics = DiagnosticsCheckBox.IsChecked == true,
                AutoCheckUpdates = AutoCheckUpdatesCheckBox.IsChecked == true,
                TranslationUnitCount = SelectedInt(TuCountBox, current.TranslationUnitCount),
                DefaultProbeBudget = SelectedInt(ProbeBudgetBox, current.DefaultProbeBudget)
            };
            await AppServices.Current.SettingsStore.SaveAsync(settings);
            AppServices.Current.Settings = settings;
            AppServices.Current.SettingsChanged?.Invoke(settings);
            var resolution = ToolchainLocator.Resolve(settings, AppServices.Current.Paths);
            SettingsStatus.Text = resolution.CanBuild
                ? "Settings saved · frontend, SDK headers and CMake are available. Confirm MSVC Build Tools before compiling."
                : "Settings saved · " + string.Join(" ", resolution.Notes);
        }
        catch (Exception ex)
        {
            SettingsStatus.Text = $"Settings could not be saved: {ex.Message}";
        }
    }

    private void Reset_Click(object sender, RoutedEventArgs e)
    {
        LibraryRootBox.Text = AppServices.Current.Paths.Library;
        FrontendPathBox.Text = string.Empty;
        RunnerPathBox.Text = string.Empty;
        SdkIncludeRootBox.Text = string.Empty;
        CMakePathBox.Text = string.Empty;
        DiagnosticsCheckBox.IsChecked = true;
        AutoCheckUpdatesCheckBox.IsChecked = true;
        SelectCombo(TuCountBox, "4");
        SelectCombo(ProbeBudgetBox, "5000000");
        SettingsStatus.Text = "Defaults loaded. Press Save settings to persist.";
    }

    private static int SelectedInt(ComboBox combo, int fallback)
    {
        if (combo.SelectedItem is ComboBoxItem item && int.TryParse(item.Tag?.ToString(), out var value))
            return value;
        return fallback;
    }
}
