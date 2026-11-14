using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.Core.Services;

namespace REPS2.Desktop.ViewModels;

public sealed class HomeViewModel(IGameLibraryService library, Func<DesktopSettings> settings) : ViewModelBase
{
    private IReadOnlyList<GameSummary> _games = [];
    private int _selectedIndex;
    private string _errorMessage = string.Empty;
    private bool _isLoading;

    public GameSummary? SelectedGame => _games.Count == 0 ? null : _games[_selectedIndex];
    public string FeaturedTitle => SelectedGame?.DisplayName ?? string.Empty;
    public string FeaturedSerial => SelectedGame?.Subtitle ?? string.Empty;
    public string FeaturedLifecycle => SelectedGame?.LifecycleLabel ?? string.Empty;
    public string FeaturedEvidence => SelectedGame?.EvidenceNote ?? string.Empty;
    public string PreviousTitle => _games.Count > 1 ? _games[PreviousIndex].DisplayName : string.Empty;
    public string NextTitle => _games.Count > 1 ? _games[NextIndex].DisplayName : string.Empty;
    public string CollectionCount => $"{_games.Count:N0} {(_games.Count == 1 ? "TITLE" : "TITLES")}";
    public string PositionLabel => _games.Count == 0 ? "0 / 0" : $"{_selectedIndex + 1:00} / {_games.Count:00}";
    public string ErrorMessage { get => _errorMessage; private set => Set(ref _errorMessage, value); }
    public bool IsLoading { get => _isLoading; private set => Set(ref _isLoading, value); }
    public bool CarouselVisible => _games.Count > 0 && string.IsNullOrEmpty(ErrorMessage);
    public bool EmptyVisible => _games.Count == 0 && string.IsNullOrEmpty(ErrorMessage) && !IsLoading;
    public bool ErrorVisible => !string.IsNullOrEmpty(ErrorMessage);
    public bool HasMultipleGames => _games.Count > 1;

    private int PreviousIndex => (_selectedIndex + _games.Count - 1) % _games.Count;
    private int NextIndex => (_selectedIndex + 1) % _games.Count;

    public async Task RefreshAsync(CancellationToken cancellationToken = default)
    {
        IsLoading = true;
        ErrorMessage = string.Empty;
        NotifyVisibilityChanged();

        try
        {
            var snapshot = await library.ScanAsync(settings().LibraryRoot, cancellationToken);
            _games = snapshot.Games
                .OrderBy(game => game.DisplayName, StringComparer.CurrentCultureIgnoreCase)
                .ToArray();
            _selectedIndex = Math.Clamp(_selectedIndex, 0, Math.Max(0, _games.Count - 1));
            NotifySelectionChanged();
        }
        catch (Exception ex)
        {
            _games = [];
            _selectedIndex = 0;
            ErrorMessage = ex.Message;
            NotifySelectionChanged();
        }
        finally
        {
            IsLoading = false;
            NotifyVisibilityChanged();
        }
    }

    public void ShowPrevious()
    {
        if (_games.Count < 2)
            return;
        _selectedIndex = PreviousIndex;
        NotifySelectionChanged();
    }

    public void ShowNext()
    {
        if (_games.Count < 2)
            return;
        _selectedIndex = NextIndex;
        NotifySelectionChanged();
    }

    private void NotifySelectionChanged()
    {
        OnPropertyChanged(nameof(SelectedGame));
        OnPropertyChanged(nameof(FeaturedTitle));
        OnPropertyChanged(nameof(FeaturedSerial));
        OnPropertyChanged(nameof(FeaturedLifecycle));
        OnPropertyChanged(nameof(FeaturedEvidence));
        OnPropertyChanged(nameof(PreviousTitle));
        OnPropertyChanged(nameof(NextTitle));
        OnPropertyChanged(nameof(CollectionCount));
        OnPropertyChanged(nameof(PositionLabel));
        OnPropertyChanged(nameof(HasMultipleGames));
        NotifyVisibilityChanged();
    }

    private void NotifyVisibilityChanged()
    {
        OnPropertyChanged(nameof(CarouselVisible));
        OnPropertyChanged(nameof(EmptyVisible));
        OnPropertyChanged(nameof(ErrorVisible));
    }
}
