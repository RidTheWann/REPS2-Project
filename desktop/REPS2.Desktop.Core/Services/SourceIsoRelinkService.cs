using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using REPS2.Desktop.Core.Models;

namespace REPS2.Desktop.Core.Services;

/// <summary>
/// Restores a moved source ISO reference only when its bytes match the
/// SHA-256 identity already recorded for the selected library entry.
/// </summary>
public static class SourceIsoRelinkService
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public static async Task RelinkAsync(
        string libraryRoot,
        GameSummary game,
        string replacementIsoPath,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(libraryRoot);
        ArgumentNullException.ThrowIfNull(game);
        ArgumentException.ThrowIfNullOrWhiteSpace(replacementIsoPath);

        if (!IsValidGameId(game.GameId))
            throw new InvalidOperationException("The library entry has an invalid content-addressed game ID.");

        var gamesRoot = Path.GetFullPath(Path.Combine(libraryRoot, "games"));
        var gameDirectory = Path.GetFullPath(Path.Combine(gamesRoot, game.GameId));
        var manifestPath = Path.GetFullPath(Path.Combine(gameDirectory, "game.json"));
        if (!IsChildPath(gameDirectory, gamesRoot) || !IsChildPath(manifestPath, gameDirectory))
            throw new InvalidOperationException("The library manifest path is outside the selected library entry.");

        if (!Directory.Exists(gameDirectory))
            throw new DirectoryNotFoundException("The selected game directory is missing from the library.");
        if (!File.Exists(manifestPath))
            throw new FileNotFoundException("The selected library manifest is missing.", manifestPath);
        EnsureNotReparsePoint(gameDirectory, "The game directory cannot be a symbolic link or reparse point.");
        EnsureNotReparsePoint(manifestPath, "The game manifest cannot be a symbolic link or reparse point.");

        var replacementPath = Path.GetFullPath(replacementIsoPath);
        if (!File.Exists(replacementPath))
            throw new FileNotFoundException("The selected replacement ISO does not exist.", replacementPath);

        var content = await File.ReadAllTextAsync(manifestPath, cancellationToken);
        var root = JsonNode.Parse(content) as JsonObject
            ?? throw new InvalidDataException("The game manifest root must be a JSON object.");
        if (ReadInt(root, "schema_version") != 1
            || !string.Equals(ReadString(root, "game_id"), game.GameId, StringComparison.Ordinal))
            throw new InvalidDataException("The selected manifest does not match this library entry.");

        var source = root["source_iso"] as JsonObject
            ?? throw new InvalidDataException("The game manifest has no source_iso object.");
        var expectedSha = ReadString(source, "sha256");
        if (!IsSha256(expectedSha)
            || !string.Equals(expectedSha, game.IsoSha256, StringComparison.OrdinalIgnoreCase)
            || !string.Equals(game.GameId, $"sha256-{expectedSha}", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The selected manifest's ISO identity does not match the content-addressed library entry.");

        var expectedSize = ReadLong(source, "size_bytes");
        if (expectedSize <= 0)
            throw new InvalidDataException("The stored source ISO byte size is missing or invalid; automatic relink is unsafe.");

        // Keep the verified file handle open until the manifest path is committed.
        // On Windows, FileShare.Read prevents another process from replacing or
        // modifying the selected file between hashing and the manifest update.
        await using var verifiedIso = new FileStream(
            replacementPath, FileMode.Open, FileAccess.Read, FileShare.Read,
            bufferSize: 1024 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan);
        if (expectedSize > 0 && verifiedIso.Length != expectedSize)
            throw new InvalidOperationException(
                $"The selected file size does not match the original ISO ({verifiedIso.Length:N0} bytes; expected {expectedSize:N0} bytes).");

        var digest = Convert.ToHexString(await SHA256.HashDataAsync(verifiedIso, cancellationToken))
            .ToLowerInvariant();
        if (!string.Equals(digest, expectedSha, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException(
                $"The selected ISO is not the original disc image. SHA-256 mismatch (actual {digest}, expected {expectedSha}).");

        cancellationToken.ThrowIfCancellationRequested();
        source["path"] = replacementPath;

        var temporaryPath = manifestPath + ".relink-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            await using (var stream = new FileStream(
                temporaryPath, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                bufferSize: 64 * 1024, FileOptions.Asynchronous | FileOptions.WriteThrough))
            {
                await JsonSerializer.SerializeAsync(stream, root, JsonOptions, cancellationToken);
                await stream.FlushAsync(cancellationToken);
            }

            cancellationToken.ThrowIfCancellationRequested();
            File.Move(temporaryPath, manifestPath, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporaryPath))
                File.Delete(temporaryPath);
        }
    }

    private static bool IsValidGameId(string value) =>
        value is { Length: 71 } && value.StartsWith("sha256-", StringComparison.Ordinal)
        && value[7..].All(Uri.IsHexDigit);

    private static bool IsSha256(string? value) =>
        value is { Length: 64 } && value.All(Uri.IsHexDigit);

    private static bool IsChildPath(string candidate, string parent)
    {
        var prefix = Path.GetFullPath(parent) + Path.DirectorySeparatorChar;
        return Path.GetFullPath(candidate).StartsWith(prefix,
            OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
    }

    private static void EnsureNotReparsePoint(string path, string message)
    {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidOperationException(message);
    }

    private static string? ReadString(JsonObject value, string name) =>
        value[name] is JsonValue item && item.TryGetValue<string>(out var result) ? result : null;

    private static int ReadInt(JsonObject value, string name) =>
        value[name] is JsonValue item && item.TryGetValue<int>(out var result) ? result : 0;

    private static long ReadLong(JsonObject value, string name) =>
        value[name] is JsonValue item && item.TryGetValue<long>(out var result) ? result : 0;
}
