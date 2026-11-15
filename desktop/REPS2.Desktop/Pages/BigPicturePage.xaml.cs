using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.ViewModels;
using Windows.System;

namespace REPS2.Desktop.Pages;

public sealed partial class BigPicturePage : Page
{
    private string? _selectedGameId;

    public HomeViewModel ViewModel { get; } = new(
        AppServices.Current.LibraryService, () => AppServices.Current.Settings);

    public BigPicturePage()
    {
        InitializeComponent();
        Loaded += BigPicturePage_Loaded;
        PageRoot.SizeChanged += PageRoot_SizeChanged;
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        _selectedGameId = e.Parameter as string;
    }

    private async void BigPicturePage_Loaded(object sender, RoutedEventArgs e)
    {
        await ViewModel.RefreshAsync();
        ViewModel.SelectGame(_selectedGameId);
        UpdateAdjacentGameCards(PageRoot.ActualWidth < 1200);
        PageRoot.Focus(FocusState.Programmatic);
    }

    private async void Refresh_Click(object sender, RoutedEventArgs e) => await ViewModel.RefreshAsync();

    private void Exit_Click(object sender, RoutedEventArgs e) => AppServices.Current.ReturnToDesktop?.Invoke();

    private void Previous_Click(object sender, RoutedEventArgs e) => ViewModel.ShowPrevious();

    private void Next_Click(object sender, RoutedEventArgs e) => ViewModel.ShowNext();

    private void OpenTitle_Click(object sender, RoutedEventArgs e)
    {
        if (ViewModel.SelectedGame is GameSummary game)
            AppServices.Current.OpenGame(game);
    }

    private void CheckReadiness_Click(object sender, RoutedEventArgs e)
    {
        if (ViewModel.SelectedGame is GameSummary game)
            AppServices.Current.OpenReadiness(game);
    }

    private void PageRoot_KeyDown(object sender, Microsoft.UI.Xaml.Input.KeyRoutedEventArgs e)
    {
        if (!ReferenceEquals(e.OriginalSource, PageRoot) && e.Key != VirtualKey.Escape)
            return;

        switch (e.Key)
        {
            case VirtualKey.Left:
                ViewModel.ShowPrevious();
                e.Handled = true;
                break;
            case VirtualKey.Right:
                ViewModel.ShowNext();
                e.Handled = true;
                break;
            case VirtualKey.Enter:
                if (ViewModel.SelectedGame is GameSummary game)
                    AppServices.Current.OpenReadiness(game);
                e.Handled = true;
                break;
            case VirtualKey.D:
                if (ViewModel.SelectedGame is GameSummary selectedGame)
                    AppServices.Current.OpenGame(selectedGame);
                e.Handled = true;
                break;
            case VirtualKey.Escape:
                AppServices.Current.ReturnToDesktop();
                e.Handled = true;
                break;
        }
    }

    private void PageRoot_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        UpdateAdjacentGameCards(e.NewSize.Width < 1200);
    }

    private void UpdateAdjacentGameCards(bool compact)
    {
        CarouselLayout.Padding = compact
            ? new Thickness(24, 18, 24, 18)
            : new Thickness(38, 18, 38, 18);

        var visible = ViewModel.HasMultipleGames && !compact
            ? Visibility.Visible
            : Visibility.Collapsed;
        PreviousPreviewCard.Visibility = visible;
        NextPreviewCard.Visibility = visible;
    }
}
