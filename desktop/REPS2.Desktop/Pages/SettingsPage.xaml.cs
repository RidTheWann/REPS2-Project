using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using REPS2.Desktop.ViewModels;

namespace REPS2.Desktop.Pages;

public sealed partial class SettingsPage : Page
{
    public SettingsViewModel ViewModel { get; } = new(AppServices.Current.SettingsStore);
    private string _activeCategory = "General";

    public SettingsPage()
    {
        InitializeComponent();
        ActualThemeChanged += (_, _) => SetCategory(_activeCategory);
        Loaded += SettingsPage_Loaded;
    }

    private void SettingsPage_Loaded(object sender, RoutedEventArgs e)
    {
        LoadSettings();
        SetCategory(_activeCategory);
    }

    private void LoadSettings()
    {
        var settings = AppServices.Current.Settings;
        SelectCombo(ThemeBox, settings.Theme);
        LibraryRootBox.Text = settings.LibraryRoot;
        FrontendPathBox.Text = settings.FrontendPath;
        RunnerPathBox.Text = settings.EngineRunnerPath;
        SdkIncludeRootBox.Text = settings.SdkIncludeRoot;
        CMakePathBox.Text = settings.CMakePath;
        DiagnosticsCheckBox.IsChecked = settings.DeveloperDiagnostics;
        ReduceMotionSwitch.IsOn = settings.ReduceMotion;
        ConfirmExitSwitch.IsOn = settings.ConfirmBeforeExit;
        SelectCombo(TuCountBox, settings.TranslationUnitCount.ToString());
        SelectCombo(ProbeBudgetBox, settings.DefaultProbeBudget.ToString());
        UpdateStatusLabel.Text = AppServices.Current.UpdateStatus;
    }

    private static void SelectCombo(ComboBox combo, string tag)
    {
        combo.SelectedItem = combo.Items.OfType<ComboBoxItem>()
            .FirstOrDefault(item => Equals(item.Tag?.ToString(), tag));
    }

    private void Category_Click(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string category })
            SetCategory(category);
    }

    private void SetCategory(string category)
    {
        _activeCategory = category;
        GeneralPanel.Visibility = category == "General" ? Visibility.Visible : Visibility.Collapsed;
        LibraryPanel.Visibility = category == "Library" ? Visibility.Visible : Visibility.Collapsed;
        ControllerPanel.Visibility = category == "Controller" ? Visibility.Visible : Visibility.Collapsed;
        AdvancedPanel.Visibility = category == "Advanced" ? Visibility.Visible : Visibility.Collapsed;

        var activeBrush = GetActiveCategoryBrush();
        GeneralCategoryButton.Background = category == "General" ? activeBrush : null;
        LibraryCategoryButton.Background = category == "Library" ? activeBrush : null;
        ControllerCategoryButton.Background = category == "Controller" ? activeBrush : null;
        AdvancedCategoryButton.Background = category == "Advanced" ? activeBrush : null;
    }

    private Brush GetActiveCategoryBrush()
    {
        var themeKey = ActualTheme == ElementTheme.Light ? "Light" : "Default";
        if (Application.Current.Resources.ThemeDictionaries[themeKey] is not ResourceDictionary themeResources)
            throw new InvalidOperationException($"The {themeKey} theme resource dictionary is unavailable.");

        return themeResources["SurfaceTwoBrush"] as Brush
            ?? throw new InvalidOperationException($"SurfaceTwoBrush is missing from the {themeKey} theme.");
    }

    private async void Save_Click(object sender, RoutedEventArgs e)
    {
        var current = AppServices.Current.Settings;
        var settings = ViewModel.Compose(
            current,
            SelectedTag(ThemeBox, current.Theme),
            AppServices.Current.Paths.Library,
            LibraryRootBox.Text,
            FrontendPathBox.Text,
            RunnerPathBox.Text,
            SdkIncludeRootBox.Text,
            CMakePathBox.Text,
            DiagnosticsCheckBox.IsChecked == true,
            current.AutoCheckUpdates,
            ReduceMotionSwitch.IsOn,
            ConfirmExitSwitch.IsOn,
            SelectedInt(TuCountBox, current.TranslationUnitCount),
            SelectedInt(ProbeBudgetBox, current.DefaultProbeBudget));

        var saved = await ViewModel.SaveAsync(settings, AppServices.Current.Paths);
        if (saved is null)
            return;

        AppServices.Current.Settings = saved;
        AppServices.Current.SettingsChanged?.Invoke(saved);
        SetCategory(_activeCategory);
    }

    private void Reset_Click(object sender, RoutedEventArgs e)
    {
        SelectCombo(ThemeBox, "Dark");
        LibraryRootBox.Text = AppServices.Current.Paths.Library;
        FrontendPathBox.Text = string.Empty;
        RunnerPathBox.Text = string.Empty;
        SdkIncludeRootBox.Text = string.Empty;
        CMakePathBox.Text = string.Empty;
        DiagnosticsCheckBox.IsChecked = true;
        ReduceMotionSwitch.IsOn = false;
        ConfirmExitSwitch.IsOn = true;
        SelectCombo(TuCountBox, "4");
        SelectCombo(ProbeBudgetBox, "5000000");
    }

    private async void CheckUpdates_Click(object sender, RoutedEventArgs e)
    {
        UpdateStatusLabel.Text = "Checking published releases…";
        await AppServices.Current.UpdateDesktopAsync();
        UpdateStatusLabel.Text = AppServices.Current.UpdateStatus;
    }

    private void OpenDeveloper_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.NavigateTo("Developer");

    private static string SelectedTag(ComboBox combo, string fallback) =>
        combo.SelectedItem is ComboBoxItem item && item.Tag is string tag ? tag : fallback;

    private static int SelectedInt(ComboBox combo, int fallback)
    {
        if (combo.SelectedItem is ComboBoxItem item && int.TryParse(item.Tag?.ToString(), out var value))
            return value;
        return fallback;
    }
}
