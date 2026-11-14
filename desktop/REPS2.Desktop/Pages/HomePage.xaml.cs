using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.Core.Services;
using REPS2.Desktop.ViewModels;
using Windows.System;

namespace REPS2.Desktop.Pages;

public sealed partial class HomePage : Page
{
    public HomeViewModel ViewModel { get; } = new(
        AppServices.Current.LibraryService, () => AppServices.Current.Settings);

    public HomePage()
    {
        InitializeComponent();
        Loaded += HomePage_Loaded;
    }

    private async void HomePage_Loaded(object sender, RoutedEventArgs e)
    {
        await ViewModel.RefreshAsync();
        PageRoot.Focus(FocusState.Programmatic);
    }

    private async void Refresh_Click(object sender, RoutedEventArgs e) => await ViewModel.RefreshAsync();

    private async void AddGame_Click(object sender, RoutedEventArgs e) => await AppServices.Current.AddGameAsync();

    private void Previous_Click(object sender, RoutedEventArgs e) => ViewModel.ShowPrevious();

    private void Next_Click(object sender, RoutedEventArgs e) => ViewModel.ShowNext();

    private void PageRoot_KeyDown(object sender, Microsoft.UI.Xaml.Input.KeyRoutedEventArgs e)
    {
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
                OpenTitle();
                e.Handled = true;
                break;
        }
    }

    private void OpenTitle_Click(object sender, RoutedEventArgs e)
    {
        OpenTitle();
    }

    private void OpenTitle()
    {
        if (ViewModel.SelectedGame is GameSummary game)
            AppServices.Current.OpenGame(game);
    }
}
