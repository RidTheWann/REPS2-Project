using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Microsoft.UI.Windowing;
using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.Api.Ipc;
using REPS2.Desktop.Core.Services;
using Windows.Storage.Pickers;
using Windows.System;
using WinRT.Interop;

namespace REPS2.Desktop;

public sealed partial class MainWindow : Window
{
    private readonly DesktopPaths _paths = new();
    private DesktopSettings _settings = new();
    private ISettingsService _settingsStore = null!;
    private bool _initialized;
    private bool _busy;
    private bool _isBigPicture;
    private bool _allowWindowClose;
    private string? _bigPictureGameId;
    private CancellationTokenSource? _buildCancellation;
    private DesktopReleaseInfo? _availableUpdate;
    private readonly UpdateService _updateService = new();

    public MainWindow()
    {
        InitializeComponent();
        AppWindow.Resize(new Windows.Graphics.SizeInt32(1440, 900));
        AppWindow.Closing += AppWindow_Closing;
        _paths.EnsureApplicationDirectories();
        _settingsStore = new SettingsStore(_paths);
        ContentFrame.Navigated += ContentFrame_Navigated;
        _ = InitializeAsync();
    }

    private async Task InitializeAsync()
    {
        try
        {
            _settings = await _settingsStore.LoadAsync();
            var jobStore = new JobStore(_paths);
            var toolchain = new ProcessToolchainClient(_paths.Root);
            var context = new DesktopAppContext(
                _paths,
                _settings,
                _settingsStore,
                new LibraryService(_paths),
                new BuildCoordinator(_paths, jobStore, new SettingsStore(_paths), toolchain),
                new SessionService(toolchain),
                new DiagnosticsService(toolchain),
                jobStore,
                toolchain,
                new HostShellService(_paths));
            context.AddGameAsync = AddGameAsync;
            context.BuildAsync = BuildGameAsync;
            context.RelinkSourceIsoAsync = RelinkSourceIsoAsync;
            context.RunProbeAsync = RunProbeAsync;
            context.OpenGame = OpenGame;
            context.OpenReadiness = game => ContentFrame.Navigate(typeof(Pages.ReadinessPage), game);
            context.EnterBigPicture = () => BigPicture_Click(this, new RoutedEventArgs());
            context.NavigateTo = Navigate;
            context.RunDeveloperCommandAsync = RunDeveloperCommandAsync;
            context.SetStatus = SetStatus;
            context.ReturnToDesktop = ReturnToDesktop;
            context.SettingsChanged = ApplySettings;
            context.UpdateDesktopAsync = UpdateDesktopAsync;
            AppServices.Current = context;
            _initialized = true;

            ApplySettings(_settings);
            SetStatus("Ready · No title is marked Boot-tested or Playable");
            SetActiveNavigation("Home");
            Navigate("Home");
            await UpdateStartupSignal.SignalReadyAsync(_paths);
            if (_settings.AutoCheckUpdates)
                _ = CheckForUpdatesAsync(silent: true);
        }
        catch (Exception ex)
        {
            SetStatus($"REPS2 Desktop could not initialize: {ex.Message}");
        }
    }

    private void ApplySettings(DesktopSettings settings)
    {
        _settings = settings;
        if (!_initialized)
            return;

        var useLightTheme = string.Equals(settings.Theme, "Light", StringComparison.OrdinalIgnoreCase);
        RootLayout.RequestedTheme = useLightTheme ? ElementTheme.Light : ElementTheme.Dark;
        var titleBarColor = useLightTheme
            ? Windows.UI.Color.FromArgb(255, 243, 244, 246)
            : Windows.UI.Color.FromArgb(255, 16, 18, 20);
        var titleBarTextColor = useLightTheme
            ? Windows.UI.Color.FromArgb(255, 22, 24, 28)
            : Windows.UI.Color.FromArgb(255, 244, 245, 246);
        AppWindow.TitleBar.BackgroundColor = titleBarColor;
        AppWindow.TitleBar.ForegroundColor = titleBarTextColor;
        AppWindow.TitleBar.ButtonBackgroundColor = titleBarColor;
        AppWindow.TitleBar.ButtonForegroundColor = titleBarTextColor;
        AppWindow.TitleBar.ButtonHoverBackgroundColor = useLightTheme
            ? Windows.UI.Color.FromArgb(255, 238, 240, 243)
            : Windows.UI.Color.FromArgb(255, 34, 38, 42);
        AppWindow.TitleBar.ButtonHoverForegroundColor = titleBarTextColor;
        AppWindow.TitleBar.ButtonPressedBackgroundColor = useLightTheme
            ? Windows.UI.Color.FromArgb(255, 215, 220, 227)
            : Windows.UI.Color.FromArgb(255, 52, 57, 62);
        AppWindow.TitleBar.ButtonPressedForegroundColor = titleBarTextColor;
        ThemeIcon.Glyph = useLightTheme ? "\uE708" : "\uE706";
        ToolTipService.SetToolTip(ThemeButton, useLightTheme ? "Switch to dark theme" : "Switch to light theme");

        var toolchain = ToolchainLocator.Resolve(_settings, _paths);
        ToolchainBadge.Text = toolchain.CanImport
            ? toolchain.CanBuild ? "Frontend + native toolchain ready" : "Frontend ready · native build setup needed"
            : "Frontend not configured";
    }

    private async void UpdateButton_Click(object sender, RoutedEventArgs e)
    {
        await UpdateDesktopAsync();
    }

    private async Task UpdateDesktopAsync()
    {
        if (_availableUpdate is not null)
            await DownloadAndApplyUpdateAsync(_availableUpdate);
        else
            await CheckForUpdatesAsync(silent: false);
    }

    private async Task CheckForUpdatesAsync(bool silent)
    {
        if (!_initialized || UpdateButton is null)
            return;

        UpdateButton.IsEnabled = false;
        UpdateButton.Content = "Checking…";
        UpdateStatusText.Text = "Checking Alpha releases";
        AppServices.Current.UpdateStatus = "Checking published releases…";
        try
        {
            var result = await _updateService.CheckForUpdatesAsync();
            _availableUpdate = result.AvailableRelease;
            if (_availableUpdate is not null)
            {
                UpdateButton.Content = $"Update {_availableUpdate.Version}";
                UpdateStatusText.Text = "Update available";
                AppServices.Current.UpdateStatus = $"Update {_availableUpdate.Version} available";
                UpdateStatusText.Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["InfoBrush"];
                SetStatus($"REPS2 Desktop {_availableUpdate.Version} is available · one-click update ready");
            }
            else
            {
                UpdateButton.Content = "Check updates";
                UpdateStatusText.Text = result.Message;
                AppServices.Current.UpdateStatus = result.Message;
                UpdateStatusText.Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["SecondaryTextBrush"];
                if (!silent)
                    SetStatus(result.Message);
            }
        }
        catch (Exception ex)
        {
            _availableUpdate = null;
            UpdateButton.Content = "Check updates";
            UpdateStatusText.Text = "Update check unavailable";
            AppServices.Current.UpdateStatus = "Update check unavailable";
            UpdateStatusText.Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["MutedTextBrush"];
            if (!silent)
                await ShowDialogAsync("Could not check for updates", ex.Message);
            SetStatus("Update check unavailable · the rest of REPS2 remains usable offline");
        }
        finally
        {
            UpdateButton.IsEnabled = true;
        }
    }

    private async Task DownloadAndApplyUpdateAsync(DesktopReleaseInfo release)
    {
        if (_busy)
            return;

        _busy = true;
        UpdateButton.IsEnabled = false;
        UpdateProgress.Visibility = Visibility.Visible;
        UpdateProgress.IsIndeterminate = true;
        UpdateButton.Content = "Downloading…";
        UpdateStatusText.Text = $"Downloading {release.Version}";
        AppServices.Current.UpdateStatus = $"Downloading {release.Version}…";
        SetStatus($"Downloading and verifying REPS2 Desktop {release.Version}…");
        try
        {
            var progress = new Progress<UpdateDownloadProgress>(value =>
            {
                DispatcherQueue.TryEnqueue(() =>
                {
                    if (value.Fraction is { } fraction)
                    {
                        UpdateProgress.IsIndeterminate = false;
                        UpdateProgress.Value = fraction * 100;
                    }
                });
            });
            var package = await _updateService.DownloadAndVerifyAsync(
                release, _paths.Updates, progress, CancellationToken.None);

            UpdateProgress.IsIndeterminate = true;
            UpdateStatusText.Text = "Verified · restarting";
            AppServices.Current.UpdateStatus = "Verified · restarting";
            UpdateButton.Content = "Applying update…";
            SetStatus("Package checksum verified · handing off to the REPS2 updater");
            var launcher = new UpdateLauncher(_paths);
            await launcher.StartApplyAsync(package.PackagePath, package.Sha256);
            Application.Current.Exit();
        }
        catch (Exception ex)
        {
            UpdateProgress.Visibility = Visibility.Collapsed;
            UpdateProgress.IsIndeterminate = false;
            UpdateProgress.Value = 0;
            UpdateButton.Content = $"Update {release.Version}";
            UpdateStatusText.Text = "Update could not be applied";
            AppServices.Current.UpdateStatus = "Update could not be applied";
            UpdateStatusText.Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["DangerBrush"];
            await ShowDialogAsync("REPS2 update failed", ex.Message);
            SetStatus("Update failed · the current installation was left in place");
        }
        finally
        {
            _busy = false;
            UpdateButton.IsEnabled = true;
        }
    }

    private void Navigation_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: string destination } || !_initialized)
            return;
        Navigate(destination);
    }

    private void Navigate(string destination)
    {
        switch (destination)
        {
            case "Welcome": ContentFrame.Navigate(typeof(Pages.WelcomePage)); break;
            case "Home": ContentFrame.Navigate(typeof(Pages.HomePage)); break;
            case "Library": ContentFrame.Navigate(typeof(Pages.LibraryPage)); break;
            case "AddGame": ContentFrame.Navigate(typeof(Pages.AddGamePage)); break;
            case "Trophy": ContentFrame.Navigate(typeof(Pages.TrophyPage)); break;
            case "Statistics": ContentFrame.Navigate(typeof(Pages.StatisticsPage)); break;
            case "Profile": ContentFrame.Navigate(typeof(Pages.ProfilePage)); break;
            case "BuildQueue": ContentFrame.Navigate(typeof(Pages.BuildQueuePage)); break;
            case "Developer": ContentFrame.Navigate(typeof(Pages.DeveloperPage)); break;
            case "EngineMonitor": ContentFrame.Navigate(typeof(Pages.EngineMonitorPage)); break;
            case "Settings": ContentFrame.Navigate(typeof(Pages.SettingsPage)); break;
        }
    }

    private void ContentFrame_Navigated(object sender, NavigationEventArgs e)
    {
        var heading = e.Content switch
        {
            Pages.WelcomePage _ => "Welcome",
            Pages.HomePage _ => "Home",
            Pages.BigPicturePage _ => "Big Picture",
            Pages.LibraryPage _ => "Library",
            Pages.AddGamePage _ => "Add game",
            Pages.TrophyPage _ => "Trophy",
            Pages.StatisticsPage _ => "Statistik",
            Pages.ProfilePage _ => "Profil",
            Pages.GameDetailPage _ => "Game detail",
            Pages.ReadinessPage _ => "Readiness",
            Pages.BuildQueuePage _ => "Build Queue",
            Pages.DeveloperPage _ => "Developer Center",
            Pages.EngineMonitorPage _ => "Engine monitor",
            Pages.SettingsPage _ => "Settings",
            _ => "REPS2"
        };
        var hideShell = (e.Content is Pages.WelcomePage or Pages.BigPicturePage) || _isBigPicture;
        ShellHeader.Visibility = hideShell ? Visibility.Collapsed : Visibility.Visible;
        ShellStatusBar.Visibility = hideShell ? Visibility.Collapsed : Visibility.Visible;
        HeaderRow.Height = new GridLength(hideShell ? 0 : 76);
        StatusRow.Height = new GridLength(hideShell ? 0 : 48);
        SetActiveNavigation(e.Content is Pages.GameDetailPage or Pages.ReadinessPage or Pages.AddGamePage ? "Library" : heading switch
        {
            "Build Queue" => "BuildQueue",
            "Developer Center" => "Developer",
            "Engine monitor" => "Developer",
            "Big Picture" => "Home",
            "Statistik" => "Statistics",
            "Profil" => "Profile",
            "Welcome" => "Home",
            _ => heading
        });
        SetStatus($"{heading} · Evidence-driven lifecycle");
    }

    private void SetActiveNavigation(string destination)
    {
        SetNavigationState(HomeNavigationButton, HomeNavigationIndicator, destination == "Home");
        SetNavigationState(LibraryNavigationButton, LibraryNavigationIndicator, destination == "Library");
        SetNavigationState(TrophyNavigationButton, TrophyNavigationIndicator, destination == "Trophy");
        SetNavigationState(StatisticsNavigationButton, StatisticsNavigationIndicator, destination == "Statistics");
        SetNavigationState(ProfileNavigationButton, ProfileNavigationIndicator, destination == "Profile");
        SetNavigationState(QueueNavigationButton, QueueNavigationIndicator, destination == "BuildQueue");
        SetNavigationState(DeveloperNavigationButton, DeveloperNavigationIndicator, destination == "Developer");
        SettingsNavigationButton.Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources[
            destination == "Settings" ? "PrimaryTextBrush" : "SecondaryTextBrush"];
    }

    private void SetNavigationState(Button button, Border indicator, bool isSelected)
    {
        button.Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources[
            isSelected ? "PrimaryTextBrush" : "SecondaryTextBrush"];
        indicator.Visibility = isSelected ? Visibility.Visible : Visibility.Collapsed;
    }

    private void ContentFrame_NavigationFailed(object sender, NavigationFailedEventArgs e)
    {
        SetStatus($"Navigation failed: {e.Exception.Message}");
        e.Handled = true;
    }

    private void AddGame_Click(object sender, RoutedEventArgs e) => Navigate("AddGame");

    private async Task AddGameAsync()
    {
        if (_busy || !_initialized)
            return;

        var resolution = ToolchainLocator.Resolve(_settings, _paths);
        if (resolution.FrontendPath is null)
        {
            await ShowDialogAsync("REPS2 frontend not found",
                "Install the portable REPS2 Windows SDK bundle, or set the reps2.exe path in Settings. The desktop shell does not contain a second implementation of the compiler.");
            Navigate("Settings");
            return;
        }

        var picker = new FileOpenPicker
        {
            SuggestedStartLocation = PickerLocationId.ComputerFolder,
            ViewMode = PickerViewMode.List
        };
        picker.FileTypeFilter.Add(".iso");
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
        var file = await picker.PickSingleFileAsync();
        if (file is null)
            return;

        _busy = true;
        AddGameButton.IsEnabled = false;
        var addGamePage = ContentFrame.Content as Pages.AddGamePage;
        addGamePage?.ClearImportError();
        addGamePage?.SetImporting(true);
        SetStatus("Importing ISO · validating the disc and boot ELF…");
        try
        {
            var result = await AppServices.Current.Toolchain.ImportGameAsync(
                resolution.FrontendPath,
                new ImportGameRequest(file.Path, _settings.LibraryRoot,
                    _settings.DeveloperDiagnostics, _paths.Logs));
            if (result.ExitCode != 0)
            {
                var error = result.CombinedOutput.Length == 0
                    ? $"The REPS2 frontend exited with code {result.ExitCode}."
                    : result.CombinedOutput;
                addGamePage?.SetImportError(error);
                await ShowDialogAsync("Import failed", error);
                SetStatus($"Import failed · exit code {result.ExitCode}");
                return;
            }

            var snapshot = await AppServices.Current.LibraryService.ScanAsync(_settings.LibraryRoot);
            if (ContentFrame.Content is Pages.LibraryPage page)
                page.ViewModel.SetSnapshot(snapshot);
            var imported = snapshot.Games.FirstOrDefault(game =>
                string.Equals(Path.GetFullPath(game.SourceIsoPath), Path.GetFullPath(file.Path),
                    OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal));
            if (imported is not null)
                OpenGame(imported);
            else
                Navigate("Library");
            SetStatus($"Import complete · {Path.GetFileName(file.Path)} · compatibility remains evidence-gated");
        }
        catch (Exception ex)
        {
            addGamePage?.SetImportError(
                ex.Message,
                "Import could not be completed",
                "Review the error details and configured paths before retrying.");
            await ShowDialogAsync("Import could not complete", ex.Message);
            SetStatus("Import failed · see the diagnostics directory");
        }
        finally
        {
            addGamePage?.SetImporting(false);
            _busy = false;
            AddGameButton.IsEnabled = true;
        }
    }

    private async Task RelinkSourceIsoAsync(GameSummary game)
    {
        if (_busy || !game.CanRelinkSourceIso || !_initialized)
            return;

        _busy = true;
        try
        {
            var picker = new FileOpenPicker
            {
                SuggestedStartLocation = PickerLocationId.ComputerFolder,
                ViewMode = PickerViewMode.List
            };
            picker.FileTypeFilter.Add(".iso");
            InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
            var file = await picker.PickSingleFileAsync();
            if (file is null)
                return;

            SetStatus($"Verifying replacement ISO identity · {game.DisplayName}");
            await SourceIsoRelinkService.RelinkAsync(_settings.LibraryRoot, game, file.Path);
            var snapshot = await AppServices.Current.LibraryService.ScanAsync(_settings.LibraryRoot);
            var updated = snapshot.Games.FirstOrDefault(item => item.GameId == game.GameId);
            if (updated is null)
                throw new InvalidOperationException("The ISO was relinked, but the library entry could not be reloaded.");

            OpenGame(updated);
            SetStatus($"ISO relinked · SHA-256 verified · {game.DisplayName}");
            await ShowDialogAsync("Original ISO relinked",
                $"The selected file matches the stored ISO SHA-256 and size. The source reference was updated without changing the game ID or other evidence.\n\nNew path:\n{file.Path}\n\nSHA-256:\n{game.IsoSha256}");
        }
        catch (Exception ex)
        {
            await ShowDialogAsync("ISO relink refused", ex.Message);
            SetStatus($"ISO relink refused · {game.DisplayName} · original manifest unchanged");
        }
        finally
        {
            _busy = false;
        }
    }

    private async Task BuildGameAsync(GameSummary game)
    {
        if (_busy)
            return;

        _busy = true;
        using var cancellation = new CancellationTokenSource();
        _buildCancellation = cancellation;
        CancelBuildButton.Visibility = Visibility.Visible;
        CancelBuildButton.IsEnabled = true;
        CancelBuildButton.Content = "Cancel build";
        SetStatus($"Building native module · {game.DisplayName}");
        try
        {
            var build = await AppServices.Current.BuildService.BuildAsync(game, cancellation.Token, onProgress: job =>
            {
                SetStatus($"{job.Stage} · {job.GameName}");
                AppServices.Current.PublishJobUpdate(job);
                return Task.CompletedTask;
            });
            await ShowDialogAsync("Native build verified",
                $"The generated native module and build identity were verified.\n\nModule:\n{build.NativeModulePath}\n\nCache key:\n{build.CacheKeySha256}\n\nThis is Native-built evidence, not a retail-title boot or gameplay result.");
            await RefreshCurrentPageAsync();
            SetStatus($"Native-built · {game.DisplayName} · Play remains unavailable");
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
        {
            await ShowDialogAsync("Native build cancelled",
                "The active REPS2 frontend or CMake process tree was stopped. The persisted job is marked Cancelled. Any previously verified build identity remains unchanged.");
            SetStatus($"Build cancelled · {game.DisplayName}");
        }
        catch (Exception ex)
        {
            await ShowDialogAsync("Native build failed", ex.Message);
            SetStatus($"Native build failed · {game.DisplayName}");
        }
        finally
        {
            if (ReferenceEquals(_buildCancellation, cancellation))
                _buildCancellation = null;
            CancelBuildButton.Visibility = Visibility.Collapsed;
            CancelBuildButton.IsEnabled = true;
            CancelBuildButton.Content = "Cancel build";
            _busy = false;
        }
    }

    private void CancelBuild_Click(object sender, RoutedEventArgs e)
    {
        if (_buildCancellation is null || _buildCancellation.IsCancellationRequested)
            return;

        CancelBuildButton.IsEnabled = false;
        CancelBuildButton.Content = "Cancelling…";
        SetStatus("Cancelling native build · stopping the active process tree…");
        _buildCancellation.Cancel();
    }

    private void ToggleTheme_Click(object sender, RoutedEventArgs e)
    {
        _ = ToggleThemeAsync();
    }

    private async Task ToggleThemeAsync()
    {
        var useLightTheme = RootLayout.RequestedTheme == ElementTheme.Dark;
        var updated = _settings with { Theme = useLightTheme ? "Light" : "Dark" };
        try
        {
            await _settingsStore.SaveAsync(updated);
            AppServices.Current.Settings = updated;
            ApplySettings(updated);
        }
        catch (Exception ex)
        {
            await ShowDialogAsync("Theme could not be saved", ex.Message);
        }
    }

    private void BigPicture_Click(object sender, RoutedEventArgs e)
    {
        if (!_isBigPicture)
        {
            _bigPictureGameId = ContentFrame.Content switch
            {
                Pages.HomePage home => home.ViewModel.SelectedGame?.GameId,
                Pages.GameDetailPage detail => detail.ViewModel.Game?.GameId,
                _ => null
            };
            ContentFrame.Navigate(typeof(Pages.BigPicturePage), _bigPictureGameId);
            AppWindow.SetPresenter(AppWindowPresenterKind.FullScreen);
            BigPictureLabel.Text = "Exit Big Picture";
            ToolTipService.SetToolTip(BigPictureButton, "Exit Big Picture mode");
            ShellHeader.Visibility = Visibility.Collapsed;
            ShellStatusBar.Visibility = Visibility.Collapsed;
            HeaderRow.Height = new GridLength(0);
            StatusRow.Height = new GridLength(0);
            _isBigPicture = true;
        }
        else
        {
            ReturnToDesktop();
        }
    }

    private void ReturnToDesktop()
    {
        if (!_isBigPicture)
            return;

        AppWindow.SetPresenter(AppWindowPresenterKind.Overlapped);
        ContentFrame.Navigate(typeof(Pages.HomePage), _bigPictureGameId);
        BigPictureLabel.Text = "Big Picture";
        ToolTipService.SetToolTip(BigPictureButton, "Enter Big Picture mode");
        ShellHeader.Visibility = Visibility.Visible;
        ShellStatusBar.Visibility = Visibility.Visible;
        HeaderRow.Height = new GridLength(76);
        StatusRow.Height = new GridLength(48);
        _isBigPicture = false;
    }

    private void RootLayout_KeyDown(object sender, Microsoft.UI.Xaml.Input.KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.F11)
        {
            BigPicture_Click(sender, e);
            e.Handled = true;
        }
        else if (e.Key == VirtualKey.Escape && _isBigPicture)
        {
            BigPicture_Click(sender, e);
            e.Handled = true;
        }
    }

    private async Task RunProbeAsync(GameSummary game)
    {
        if (_busy || !game.CanRunProbe || string.IsNullOrWhiteSpace(game.NativeModulePath))
            return;

        var confirmation = new ContentDialog
        {
            Title = "Run trusted native code?",
            Content = "The generated module executes as native code with the current user's privileges. RG REPS2 ENGINE is not a sandbox. Continue only if this module was generated from an ISO and REPS2 source you trust.",
            PrimaryButtonText = "Run bounded probe",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Close,
            XamlRoot = ContentFrame.XamlRoot
        };
        if (await confirmation.ShowAsync() != ContentDialogResult.Primary)
            return;

        var resolution = ToolchainLocator.Resolve(_settings, _paths);
        if (resolution.EngineRunnerPath is null)
        {
            await ShowDialogAsync("Engine runner not found",
                "The RG REPS2 ENGINE runner is not installed. Add the portable SDK bundle or set its path in Settings.");
            return;
        }

        _busy = true;
        SetStatus($"Running bounded native probe · {game.DisplayName}");
        try
        {
            var report = await AppServices.Current.SessionService.RunBoundedProbeAsync(new BoundedProbeRequest(
                game, resolution.EngineRunnerPath, _paths.Root, _paths.Reports,
                Math.Clamp(_settings.DefaultProbeBudget, 1, 100_000_000)));
            await ShowDialogAsync("Bounded execution probe finished",
                $"Outcome: {report.Outcome}\nPC: {report.ProgramCounterHex ?? "unknown"}\nInstructions: {report.InstructionsExecuted}\n\nReport:\n{report.ReportPath}\n\nA bounded probe is not boot evidence. REPS2 will not promote this title to Boot-tested or Playable automatically.");
            SetStatus($"Probe finished · {game.DisplayName} · report saved");
        }
        catch (ProbeExecutionException ex)
        {
            await ShowDialogAsync("Execution probe failed",
                $"The engine runner exited with code {ex.Probe.Invocation.ExitCode}.\n\n{ex.Probe.Invocation.CombinedOutput}\n\nReport:\n{ex.Probe.ReportPath}");
            SetStatus($"Probe failed · exit {ex.Probe.Invocation.ExitCode}");
        }
        catch (Exception ex)
        {
            await ShowDialogAsync("Execution probe failed", ex.Message);
            SetStatus("Execution probe failed");
        }
        finally
        {
            _busy = false;
        }
    }

    private async Task RunDeveloperCommandAsync(string command)
    {
        var resolution = ToolchainLocator.Resolve(_settings, _paths);
        if (resolution.FrontendPath is null)
        {
            await ShowDialogAsync("Frontend unavailable", "Set the reps2.exe path in Settings.");
            return;
        }
        try
        {
            var result = await AppServices.Current.DiagnosticsService.RunCommandAsync(
                resolution.FrontendPath, command, _paths.Logs);
            if (ContentFrame.Content is Pages.DeveloperPage page)
                page.ViewModel.SetOutput(command, result.ExitCode, result.CombinedOutput);
            SetStatus($"Developer command · {command} · exit {result.ExitCode}");
        }
        catch (Exception ex)
        {
            if (ContentFrame.Content is Pages.DeveloperPage page)
                page.ViewModel.SetOutput(command, 1, ex.Message);
            SetStatus($"Developer command failed · {command}");
        }
    }

    private void OpenGame(GameSummary game) => ContentFrame.Navigate(typeof(Pages.GameDetailPage), game);

    private async Task RefreshCurrentPageAsync()
    {
        if (ContentFrame.Content is Pages.LibraryPage library)
            await library.RefreshAsync();
        else if (ContentFrame.Content is Pages.GameDetailPage detail)
            await detail.RefreshEvidenceAsync();
        else if (ContentFrame.Content is Pages.BuildQueuePage queue)
            await queue.RefreshAsync();
    }

    private void SetStatus(string message)
    {
        StatusMessage.Text = message;
        StatusText.Text = "DESKTOP ALPHA";
    }

    private async void AppWindow_Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (!_settings.ConfirmBeforeExit || _allowWindowClose)
            return;

        args.Cancel = true;
        var dialog = new ContentDialog
        {
            Title = "Exit REPS2 Desktop?",
            Content = "Your imported library and original ISO files will remain unchanged.",
            PrimaryButtonText = "Exit",
            CloseButtonText = "Stay",
            DefaultButton = ContentDialogButton.Close,
            XamlRoot = ContentFrame.XamlRoot
        };
        if (await dialog.ShowAsync() == ContentDialogResult.Primary)
        {
            _allowWindowClose = true;
            Application.Current.Exit();
        }
    }

    private async Task ShowDialogAsync(string title, string message)
    {
        var dialog = new ContentDialog
        {
            Title = title,
            Content = new ScrollViewer
            {
                MaxHeight = 420,
                Content = new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true }
            },
            CloseButtonText = "Close",
            XamlRoot = ContentFrame.XamlRoot
        };
        await dialog.ShowAsync();
    }
}
