using System.Collections.ObjectModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using REPS2.Desktop.Core.Models;
using REPS2.Desktop.Core.Services;

namespace REPS2.Desktop.Pages;

public sealed partial class LibraryPage : Page
{
    private IReadOnlyList<GameSummary> _allGames = [];
    private bool _isReady;
    public ObservableCollection<GameSummary> VisibleGames { get; } = [];

    public LibraryPage()
    {
        InitializeComponent();
        Loaded += LibraryPage_Loaded;
    }

    private async void LibraryPage_Loaded(object sender, RoutedEventArgs e)
    {
        _isReady = true;
        ApplyFilter();
        await RefreshAsync();
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        if (e.Parameter is LibrarySnapshot snapshot)
            SetSnapshot(snapshot);
    }

    public async Task RefreshAsync()
    {
        try
        {
            var settings = AppServices.Current.Settings;
            var snapshot = await AppServices.Current.LibraryService.ScanAsync(settings.LibraryRoot);
            SetSnapshot(snapshot);
        }
        catch (Exception ex)
        {
            EmptyStateTitle.Text = "Library could not be read";
            EmptyStateDescription.Text = ex.Message;
            EmptyStatePanel.Visibility = Visibility.Visible;
            GamesGrid.Visibility = Visibility.Collapsed;
        }
    }

    public void SetSnapshot(LibrarySnapshot snapshot)
    {
        _allGames = snapshot.Games;
        LibraryCount.Text = $"{_allGames.Count} titles";
        BuiltCount.Text = $"{_allGames.Count(game => game.Lifecycle is GameLifecycle.NativeBuilt or GameLifecycle.ExecutionBlocked or GameLifecycle.BootTested or GameLifecycle.Playable)} titles";
        PlayableCount.Text = $"{_allGames.Count(game => game.Lifecycle == GameLifecycle.Playable)} titles";
        LibraryNotice.Text = snapshot.Warnings.Count == 0
            ? $"Library: {snapshot.LibraryRoot}   ·   {snapshot.Warnings.Count} integrity warnings"
            : string.Join("  ·  ", snapshot.Warnings.Take(3));
        ApplyFilter();
    }

    private void ApplyFilter()
    {
        var filter = Enum.TryParse<LibraryFilterMode>(LibraryFilterBox.SelectedValue?.ToString(), true, out var selectedFilter)
            ? selectedFilter : LibraryFilterMode.All;
        var sort = Enum.TryParse<LibrarySortMode>(LibrarySortBox.SelectedValue?.ToString(), true, out var selectedSort)
            ? selectedSort : LibrarySortMode.NameAscending;
        var filtered = LibraryCatalogView.Apply(_allGames, SearchBox.Text, filter, sort);

        VisibleGames.Clear();
        foreach (var game in filtered)
            VisibleGames.Add(game);

        var hasGames = _allGames.Count > 0;
        var hasResults = filtered.Count > 0;
        var hasActiveFilter = !string.IsNullOrWhiteSpace(SearchBox.Text) || filter != LibraryFilterMode.All;
        ResultsSummary.Text = $"{filtered.Count:N0} of {_allGames.Count:N0} titles";
        GamesGrid.Visibility = hasResults ? Visibility.Visible : Visibility.Collapsed;
        EmptyStatePanel.Visibility = hasResults ? Visibility.Collapsed : Visibility.Visible;
        EmptyAddButton.Visibility = hasGames ? Visibility.Collapsed : Visibility.Visible;
        ClearFiltersButton.Visibility = hasGames && hasActiveFilter ? Visibility.Visible : Visibility.Collapsed;
        EmptyStateTitle.Text = hasGames ? "No titles match these filters" : "Your library is ready";
        EmptyStateDescription.Text = hasGames
            ? "Change the search or lifecycle filter, or clear the filters to see your full library."
            : "Add one raw PS2 ISO you own. REPS2 validates it and leaves the source file untouched.";
    }

    private void SearchBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        if (_isReady)
            ApplyFilter();
    }

    private void Filters_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_isReady)
            ApplyFilter();
    }

    private void ClearFilters_Click(object sender, RoutedEventArgs e)
    {
        SearchBox.Text = string.Empty;
        LibraryFilterBox.SelectedIndex = 0;
        LibrarySortBox.SelectedIndex = 0;
        ApplyFilter();
    }
    private async void Refresh_Click(object sender, RoutedEventArgs e) => await RefreshAsync();
    private async void AddGame_Click(object sender, RoutedEventArgs e) => await AppServices.Current.AddGameAsync();

    private void GamesGrid_ItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is GameSummary game)
            AppServices.Current.OpenGame(game);
    }
}
