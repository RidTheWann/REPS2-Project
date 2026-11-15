using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.ViewModels;

namespace REPS2.Desktop.Pages;

public sealed partial class TrophyPage : Page
{
    public HomeViewModel ViewModel { get; } = new(
        AppServices.Current.LibraryService, () => AppServices.Current.Settings);

    public TrophyPage()
    {
        InitializeComponent();
        Loaded += TrophyPage_Loaded;
    }

    private async void TrophyPage_Loaded(object sender, RoutedEventArgs e)
    {
        await ViewModel.RefreshAsync();
        GamesList.SelectedItem = ViewModel.SelectedGame;
        UpdateSelection(ViewModel.SelectedGame);
    }

    private void GamesList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (e.AddedItems.FirstOrDefault() is not GameSummary game)
            return;

        ViewModel.SelectGame(game.GameId);
        UpdateSelection(game);
    }

    private void UpdateSelection(GameSummary? game)
    {
        SelectedGameText.Text = game?.DisplayName ?? string.Empty;
        SelectedGameText.Visibility = game is null ? Visibility.Collapsed : Visibility.Visible;
        ViewGameDetailsButton.IsEnabled = game is not null;
    }

    private void ViewGameDetails_Click(object sender, RoutedEventArgs e)
    {
        if (ViewModel.SelectedGame is { } game)
            AppServices.Current.OpenGame(game);
    }

    private void AddGame_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.NavigateTo("AddGame");
}
