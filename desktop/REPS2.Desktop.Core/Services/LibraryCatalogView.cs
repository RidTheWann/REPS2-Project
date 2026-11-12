using REPS2.Desktop.Core.Models;

namespace REPS2.Desktop.Core.Services;

public enum LibraryFilterMode
{
    All,
    NeedsReview,
    ReadyToBuild,
    NativeBuilt,
    Playable
}

public enum LibrarySortMode
{
    NameAscending,
    NameDescending
}

/// <summary>
/// Applies the Library screen's search, evidence-state filters and stable
/// alphabetical ordering without changing any compatibility evidence.
/// </summary>
public static class LibraryCatalogView
{
    public static IReadOnlyList<GameSummary> Apply(
        IEnumerable<GameSummary> games,
        string? search,
        LibraryFilterMode filter,
        LibrarySortMode sort)
    {
        ArgumentNullException.ThrowIfNull(games);

        IEnumerable<GameSummary> query = games;
        var term = search?.Trim();
        if (!string.IsNullOrWhiteSpace(term))
        {
            query = query.Where(game =>
                Contains(game.DisplayName, term)
                || Contains(game.SerialCandidate, term)
                || Contains(game.VolumeIdentifier, term)
                || Contains(game.LifecycleLabel, term)
                || Contains(game.EvidenceNote, term)
                || Contains(Path.GetFileName(game.SourceIsoPath), term));
        }

        query = filter switch
        {
            LibraryFilterMode.NeedsReview => query.Where(IsNeedsReview),
            LibraryFilterMode.ReadyToBuild => query.Where(game => game.CanBuild),
            LibraryFilterMode.NativeBuilt => query.Where(game =>
                game.Lifecycle is GameLifecycle.NativeBuilt
                    or GameLifecycle.ExecutionBlocked
                    or GameLifecycle.BootTested
                    or GameLifecycle.Playable),
            LibraryFilterMode.Playable => query.Where(game => game.Lifecycle == GameLifecycle.Playable),
            _ => query
        };

        return sort switch
        {
            LibrarySortMode.NameDescending => query
                .OrderByDescending(game => game.DisplayName, StringComparer.CurrentCultureIgnoreCase)
                .ThenBy(game => game.GameId, StringComparer.Ordinal)
                .ToArray(),
            _ => query
                .OrderBy(game => game.DisplayName, StringComparer.CurrentCultureIgnoreCase)
                .ThenBy(game => game.GameId, StringComparer.Ordinal)
                .ToArray()
        };
    }

    private static bool IsNeedsReview(GameSummary game) =>
        game.Lifecycle is GameLifecycle.AnalysisFailed
            or GameLifecycle.TranslationIncomplete
            or GameLifecycle.NativeBuildFailed
            or GameLifecycle.ExecutionBlocked
            or GameLifecycle.RecoveryRequired
            or GameLifecycle.Unknown;

    private static bool Contains(string? value, string term) =>
        value?.Contains(term, StringComparison.CurrentCultureIgnoreCase) == true;
}
