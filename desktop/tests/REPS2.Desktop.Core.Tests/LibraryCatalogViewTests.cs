using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.Core.Services;
using Xunit;

namespace REPS2.Desktop.Core.Tests;

public sealed class LibraryCatalogViewTests
{
    [Fact]
    public void ReadyToBuildIncludesTranslatedAndRetryableNativeBuildFailures()
    {
        var games = new[]
        {
            Game("Translated", GameLifecycle.Translated),
            Game("Failed build", GameLifecycle.NativeBuildFailed),
            Game("Imported", GameLifecycle.Imported),
            Game("Built", GameLifecycle.NativeBuilt)
        };

        var result = LibraryCatalogView.Apply(
            games, null, LibraryFilterMode.ReadyToBuild, LibrarySortMode.NameAscending);

        Assert.Equal(new[] { "Failed build", "Translated" }, result.Select(game => game.DisplayName));
    }

    [Fact]
    public void NativeBuiltFilterIncludesBlockedExecutionButNeverClaimsPlayable()
    {
        var games = new[]
        {
            Game("Blocked", GameLifecycle.ExecutionBlocked),
            Game("Built", GameLifecycle.NativeBuilt),
            Game("Playable", GameLifecycle.Playable),
            Game("Translated", GameLifecycle.Translated)
        };

        var result = LibraryCatalogView.Apply(
            games, null, LibraryFilterMode.NativeBuilt, LibrarySortMode.NameAscending);

        Assert.Equal(new[] { "Blocked", "Built", "Playable" }, result.Select(game => game.DisplayName));
        Assert.All(result, game => Assert.False(game.Lifecycle == GameLifecycle.Translated));
    }

    [Fact]
    public void NeedsReviewFilterCollectsIncompleteAndRecoveryStates()
    {
        var games = new[]
        {
            Game("Analysis issue", GameLifecycle.AnalysisFailed),
            Game("Missing source", GameLifecycle.RecoveryRequired),
            Game("Partial translation", GameLifecycle.TranslationIncomplete),
            Game("Healthy native build", GameLifecycle.NativeBuilt),
            Game("Imported", GameLifecycle.Imported)
        };

        var result = LibraryCatalogView.Apply(
            games, null, LibraryFilterMode.NeedsReview, LibrarySortMode.NameAscending);

        Assert.Equal(
            new[] { "Analysis issue", "Missing source", "Partial translation" },
            result.Select(game => game.DisplayName));
    }

    [Fact]
    public void SearchAndSortComposeWithLifecycleFilter()
    {
        var games = new[]
        {
            Game("Zeta Racer", GameLifecycle.Translated, serial: "SLUS_100.01"),
            Game("Alpha Racer", GameLifecycle.Translated, serial: "SLUS_100.02"),
            Game("Alpha Puzzle", GameLifecycle.Translated, serial: "SLUS_200.01"),
            Game("Alpha Imported", GameLifecycle.Imported, serial: "SLUS_100.03")
        };

        var ascending = LibraryCatalogView.Apply(
            games, "SLUS_100", LibraryFilterMode.ReadyToBuild, LibrarySortMode.NameAscending);
        var descending = LibraryCatalogView.Apply(
            games, "racer", LibraryFilterMode.ReadyToBuild, LibrarySortMode.NameDescending);

        Assert.Equal(new[] { "Alpha Racer", "Zeta Racer" }, ascending.Select(game => game.DisplayName));
        Assert.Equal(new[] { "Zeta Racer", "Alpha Racer" }, descending.Select(game => game.DisplayName));
    }

    [Fact]
    public void PlayableFilterRequiresTheExactPlayableLifecycleState()
    {
        var games = new[]
        {
            Game("Boot tested", GameLifecycle.BootTested),
            Game("Native built", GameLifecycle.NativeBuilt),
            Game("Playable", GameLifecycle.Playable)
        };

        var result = LibraryCatalogView.Apply(
            games, null, LibraryFilterMode.Playable, LibrarySortMode.NameAscending);

        Assert.Single(result);
        Assert.Equal("Playable", result[0].DisplayName);
    }

    private static GameSummary Game(string name, GameLifecycle lifecycle, string? serial = null) =>
        new()
        {
            GameId = name.ToLowerInvariant().Replace(' ', '-'),
            DisplayName = name,
            Lifecycle = lifecycle,
            SerialCandidate = serial,
            SourceIsoPath = $"{name}.iso"
        };
}
