using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.Core.Services;
using REPS2.Desktop.ViewModels;

namespace REPS2.Desktop.Pages;

public sealed partial class LibraryPage : Page
{
    private bool _isReady;

    public LibraryViewModel ViewModel { get; } = new(
        AppServices.Current.LibraryService, () => AppServices.Current.Settings);

    public LibraryPage()
    {
        InitializeComponent();
        Loaded += LibraryPage_Loaded;
    }

    private async void LibraryPage_Loaded(object sender, RoutedEventArgs e)
    {
        _isReady = true;
        ApplyQuery();
        await RefreshAsync();
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        if (e.Parameter is LibrarySnapshot snapshot)
            ViewModel.SetSnapshot(snapshot);
    }

    public Task RefreshAsync() => ViewModel.RefreshAsync();

    private void ApplyQuery()
    {
        var filter = Enum.TryParse<LibraryFilterMode>(LibraryFilterBox.SelectedValue?.ToString(), true, out var selectedFilter)
            ? selectedFilter : LibraryFilterMode.All;
        var sort = Enum.TryParse<LibrarySortMode>(LibrarySortBox.SelectedValue?.ToString(), true, out var selectedSort)
            ? selectedSort : LibrarySortMode.NameAscending;
        ViewModel.SetQuery(SearchBox.Text, filter, sort);
    }

    private void SearchBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        if (_isReady)
            ApplyQuery();
    }

    private void Filters_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_isReady)
            ApplyQuery();
    }

    private void ClearFilters_Click(object sender, RoutedEventArgs e)
    {
        SearchBox.Text = string.Empty;
        LibraryFilterBox.SelectedIndex = 0;
        LibrarySortBox.SelectedIndex = 0;
        ViewModel.ClearFilters();
    }

    private async void Refresh_Click(object sender, RoutedEventArgs e) => await RefreshAsync();
    private async void AddGame_Click(object sender, RoutedEventArgs e) => await AppServices.Current.AddGameAsync();

    private void GamesGrid_ItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is GameSummary game)
            AppServices.Current.OpenGame(game);
    }
}
