using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.ViewModels;

namespace REPS2.Desktop.Pages;

public sealed partial class GameDetailPage : Page
{
    public GameDetailViewModel ViewModel { get; } = new();

    public GameDetailPage()
    {
        InitializeComponent();
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        if (e.Parameter is GameSummary game)
            ViewModel.Load(game);
    }

    public async Task RefreshEvidenceAsync()
    {
        if (ViewModel.Game is null)
            return;
        var snapshot = await AppServices.Current.LibraryService.ScanAsync(AppServices.Current.Settings.LibraryRoot);
        var updated = snapshot.Games.FirstOrDefault(item => item.GameId == ViewModel.Game.GameId);
        if (updated is not null)
            ViewModel.Load(updated);
    }

    private async void Relink_Click(object sender, RoutedEventArgs e)
    {
        if (ViewModel.Game is { CanRelinkSourceIso: true } game)
            await AppServices.Current.RelinkSourceIsoAsync(game);
    }

    private async void Build_Click(object sender, RoutedEventArgs e)
    {
        if (ViewModel.Game is { } game)
            await AppServices.Current.BuildAsync(game);
    }

    private async void Probe_Click(object sender, RoutedEventArgs e)
    {
        if (ViewModel.Game is { } game)
            await AppServices.Current.RunProbeAsync(game);
    }

    private void Readiness_Click(object sender, RoutedEventArgs e)
    {
        if (ViewModel.Game is { } game)
            AppServices.Current.OpenReadiness(game);
    }

    private async void ViewIso_Click(object sender, RoutedEventArgs e)
    {
        var game = ViewModel.Game;
        if (game is null)
            return;

        var content = new StackPanel { Spacing = 8 };
        content.Children.Add(new TextBlock
        {
            Text = "This is the recorded owner-supplied source image. REPS2 does not move or copy it.",
            TextWrapping = Microsoft.UI.Xaml.TextWrapping.Wrap
        });
        content.Children.Add(new TextBlock
        {
            Text = game.SourceIsoPath,
            IsTextSelectionEnabled = true,
            TextWrapping = Microsoft.UI.Xaml.TextWrapping.Wrap
        });
        content.Children.Add(new TextBlock
        {
            Text = $"SHA-256: {game.IsoSha256}",
            IsTextSelectionEnabled = true,
            TextWrapping = Microsoft.UI.Xaml.TextWrapping.Wrap
        });

        var dialog = new ContentDialog
        {
            Title = "Original ISO reference",
            Content = content,
            CloseButtonText = "Close",
            XamlRoot = XamlRoot
        };
        await dialog.ShowAsync();
    }
}
