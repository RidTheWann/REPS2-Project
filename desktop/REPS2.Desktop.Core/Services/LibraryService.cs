using REPS2.Desktop.Core.Models;

namespace REPS2.Desktop.Core.Services;

public sealed class LibraryService(DesktopPaths paths)
{
    public async Task<LibrarySnapshot> ScanAsync(string libraryRoot, CancellationToken cancellationToken = default)
    {
        var gamesRoot = Path.Combine(libraryRoot, "games");
        var games = new List<GameSummary>();
        var warnings = new List<string>();

        if (!Directory.Exists(gamesRoot))
            return new LibrarySnapshot(games, warnings, libraryRoot);

        IEnumerable<string> manifests;
        try
        {
            manifests = Directory.EnumerateDirectories(gamesRoot)
                .Select(directory => Path.Combine(directory, "game.json"))
                .Where(File.Exists)
                .ToArray();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            warnings.Add($"The library could not be enumerated: {ex.Message}");
            return new LibrarySnapshot(games, warnings, libraryRoot);
        }

        foreach (var manifest in manifests)
        {
            cancellationToken.ThrowIfCancellationRequested();
            try
            {
                var gameId = Path.GetFileName(Path.GetDirectoryName(manifest)!);
                var buildRecord = Path.Combine(paths.Builds, gameId + ".json");
                games.Add(CompatibilityStateDeriver.Parse(manifest, buildRecord));
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Text.Json.JsonException or ArgumentException)
            {
                warnings.Add($"{Path.GetFileName(Path.GetDirectoryName(manifest)!)}: {ex.Message}");
            }
            await Task.Yield();
        }

        var ordered = games
            .OrderBy(game => game.DisplayName, StringComparer.CurrentCultureIgnoreCase)
            .ToArray();
        return new LibrarySnapshot(ordered, warnings, libraryRoot);
    }
}
