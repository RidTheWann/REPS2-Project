using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using REPS2.Desktop.Core.Models;

namespace REPS2.Desktop.Pages;

public sealed partial class GameDetailPage : Page
{
    private GameSummary? _game;

    public GameDetailPage()
    {
        InitializeComponent();
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        if (e.Parameter is GameSummary game)
        {
            _game = game;
            Render(game);
        }
    }

    public async Task RefreshEvidenceAsync()
    {
        if (_game is null)
            return;
        var snapshot = await AppServices.Current.LibraryService.ScanAsync(AppServices.Current.Settings.LibraryRoot);
        var updated = snapshot.Games.FirstOrDefault(item => item.GameId == _game.GameId);
        if (updated is not null)
        {
            _game = updated;
            Render(updated);
        }
    }

    private void Render(GameSummary game)
    {
        GameTitle.Text = game.DisplayName;
        GameSerial.Text = $"{game.Subtitle}   ·   {game.LifecycleLabel}";
        LifecycleText.Text = game.LifecycleLabel;
        EvidenceText.Text = game.EvidenceNote;
        RecoveryText.Text = game.RecoveryReason ?? game.AnalysisError ?? string.Empty;
        IsoPath.Text = game.SourceIsoPath;
        IsoDigest.Text = game.IsoSha256;
        EntryPoint.Text = game.BootEntryPoint ?? "Not recorded";
        BlocksValue.Text = game.BasicBlockCount.ToString("N0");
        InstructionsValue.Text = game.DecodedInstructionCount.ToString("N0");
        UnsupportedValue.Text = $"Unsupported blocks: {game.UnsupportedBlockCount:N0}   ·   unresolved indirect: {game.UnresolvedIndirectCount:N0}";
        BuildStateText.Text = game.Lifecycle is GameLifecycle.NativeBuilt or GameLifecycle.ExecutionBlocked
            ? "Native module and build identity verified"
            : "No verified native module";
        BuildCacheKey.Text = game.CacheKeySha256 is null ? "Cache key: not available" : $"Cache key: {game.CacheKeySha256}";
        BuildModulePath.Text = game.NativeModulePath ?? "Native module path not recorded";
        RelinkButton.Visibility = game.CanRelinkSourceIso ? Visibility.Visible : Visibility.Collapsed;
        BuildButton.Visibility = game.CanBuild ? Visibility.Visible : Visibility.Collapsed;
        ProbeButton.Visibility = game.CanRunProbe ? Visibility.Visible : Visibility.Collapsed;
        BuildButton.IsEnabled = game.CanBuild;
        ProbeButton.IsEnabled = game.CanRunProbe;
    }

    private async void Relink_Click(object sender, RoutedEventArgs e)
    {
        if (_game is not null && _game.CanRelinkSourceIso)
            await AppServices.Current.RelinkSourceIsoAsync(_game);
    }

    private async void Build_Click(object sender, RoutedEventArgs e)
    {
        if (_game is not null)
            await AppServices.Current.BuildAsync(_game);
    }

    private async void Probe_Click(object sender, RoutedEventArgs e)
    {
        if (_game is not null)
            await AppServices.Current.RunProbeAsync(_game);
    }
}
