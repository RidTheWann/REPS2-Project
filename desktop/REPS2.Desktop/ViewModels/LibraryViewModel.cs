using System.Collections.ObjectModel;
using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.Core.Services;

namespace REPS2.Desktop.ViewModels;

public sealed class LibraryViewModel(IGameLibraryService library, Func<DesktopSettings> settings) : ViewModelBase
{
    private IReadOnlyList<GameSummary> _allGames = [];
    private string _libraryCount = "0 titles";
    private string _builtCount = "0 titles";
    private string _playableCount = "0 titles";
    private string _notice = string.Empty;
    private string _resultsSummary = "0 of 0 titles";
    private string _emptyStateTitle = "Your library is ready";
    private string _emptyStateDescription = string.Empty;
    private bool _gamesVisible;
    private bool _emptyVisible = true;
    private bool _emptyAddVisible = true;
    private bool _clearFiltersVisible;
    private LibraryFilterMode _filter = LibraryFilterMode.All;
    private LibrarySortMode _sort = LibrarySortMode.NameAscending;
    private string _search = string.Empty;

    public ObservableCollection<GameSummary> VisibleGames { get; } = [];

    public string LibraryCount { get => _libraryCount; private set => Set(ref _libraryCount, value); }
    public string BuiltCount { get => _builtCount; private set => Set(ref _builtCount, value); }
    public string PlayableCount { get => _playableCount; private set => Set(ref _playableCount, value); }
    public string Notice { get => _notice; private set => Set(ref _notice, value); }
    public string ResultsSummary { get => _resultsSummary; private set => Set(ref _resultsSummary, value); }
    public string EmptyStateTitle { get => _emptyStateTitle; private set => Set(ref _emptyStateTitle, value); }
    public string EmptyStateDescription { get => _emptyStateDescription; private set => Set(ref _emptyStateDescription, value); }
    public bool GamesVisible { get => _gamesVisible; private set => Set(ref _gamesVisible, value); }
    public bool EmptyVisible { get => _emptyVisible; private set => Set(ref _emptyVisible, value); }
    public bool EmptyAddVisible { get => _emptyAddVisible; private set => Set(ref _emptyAddVisible, value); }
    public bool ClearFiltersVisible { get => _clearFiltersVisible; private set => Set(ref _clearFiltersVisible, value); }

    public async Task RefreshAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            var snapshot = await library.ScanAsync(settings().LibraryRoot, cancellationToken);
            SetSnapshot(snapshot);
        }
        catch (Exception ex)
        {
            EmptyStateTitle = "Library could not be read";
            EmptyStateDescription = ex.Message;
            EmptyVisible = true;
            GamesVisible = false;
        }
    }

    public void SetSnapshot(LibrarySnapshot snapshot)
    {
        _allGames = snapshot.Games;
        LibraryCount = $"{_allGames.Count} titles";
        BuiltCount = $"{_allGames.Count(game => game.Lifecycle is GameLifecycle.NativeBuilt or GameLifecycle.ExecutionBlocked or GameLifecycle.BootTested or GameLifecycle.Playable)} titles";
        PlayableCount = $"{_allGames.Count(game => game.Lifecycle == GameLifecycle.Playable)} titles";
        Notice = snapshot.Warnings.Count == 0
            ? $"Library: {snapshot.LibraryRoot}   ·   {snapshot.Warnings.Count} integrity warnings"
            : string.Join("  ·  ", snapshot.Warnings.Take(3));
        ApplyFilter();
    }

    public void SetQuery(string search, LibraryFilterMode filter, LibrarySortMode sort)
    {
        _search = search;
        _filter = filter;
        _sort = sort;
        ApplyFilter();
    }

    public void ClearFilters()
    {
        _search = string.Empty;
        _filter = LibraryFilterMode.All;
        _sort = LibrarySortMode.NameAscending;
        ApplyFilter();
    }

    private void ApplyFilter()
    {
        var filtered = LibraryCatalogView.Apply(_allGames, _search, _filter, _sort);

        VisibleGames.Clear();
        foreach (var game in filtered)
            VisibleGames.Add(game);

        var hasGames = _allGames.Count > 0;
        var hasResults = filtered.Count > 0;
        var hasActiveFilter = !string.IsNullOrWhiteSpace(_search) || _filter != LibraryFilterMode.All;
        ResultsSummary = $"{filtered.Count:N0} of {_allGames.Count:N0} titles";
        GamesVisible = hasResults;
        EmptyVisible = !hasResults;
        EmptyAddVisible = !hasGames;
        ClearFiltersVisible = hasGames && hasActiveFilter;
        EmptyStateTitle = hasGames ? "No titles match these filters" : "Your library is ready";
        EmptyStateDescription = hasGames
            ? "Change the search or lifecycle filter, or clear the filters to see your full library."
            : "Add one raw PS2 ISO you own. REPS2 validates it and leaves the source file untouched.";
    }
}
