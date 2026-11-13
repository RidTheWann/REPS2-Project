using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using REPS2.Desktop.ViewModels;

namespace REPS2.Desktop.Pages;

public sealed partial class SettingsPage : Page
{
    public SettingsViewModel ViewModel { get; } = new(AppServices.Current.SettingsStore);

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
        var current = AppServices.Current.Settings;
        var settings = ViewModel.Compose(
            current,
            AppServices.Current.Paths.Library,
            LibraryRootBox.Text,
            FrontendPathBox.Text,
            RunnerPathBox.Text,
            SdkIncludeRootBox.Text,
            CMakePathBox.Text,
            DiagnosticsCheckBox.IsChecked == true,
            AutoCheckUpdatesCheckBox.IsChecked == true,
            SelectedInt(TuCountBox, current.TranslationUnitCount),
            SelectedInt(ProbeBudgetBox, current.DefaultProbeBudget));

        var saved = await ViewModel.SaveAsync(settings, AppServices.Current.Paths);
        if (saved is null)
            return;
        AppServices.Current.Settings = saved;
        AppServices.Current.SettingsChanged?.Invoke(saved);
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
    }

    private static int SelectedInt(ComboBox combo, int fallback)
    {
        if (combo.SelectedItem is ComboBoxItem item && int.TryParse(item.Tag?.ToString(), out var value))
            return value;
        return fallback;
    }
}
