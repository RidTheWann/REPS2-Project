using REPS2.Desktop.Api.Contracts;

namespace REPS2.Desktop.Core.Services;

public sealed class SettingsStore(DesktopPaths paths) : ISettingsService
{
    private static readonly System.Text.Json.JsonSerializerOptions JsonOptions =
        new(System.Text.Json.JsonSerializerDefaults.Web) { WriteIndented = true };

    public async Task<DesktopSettings> LoadAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            if (!File.Exists(paths.Settings))
                return new DesktopSettings { LibraryRoot = paths.Library };

            await using var stream = File.OpenRead(paths.Settings);
            var stored = await System.Text.Json.JsonSerializer.DeserializeAsync<DesktopSettings>(
                stream, JsonOptions, cancellationToken);
            var result = stored ?? new DesktopSettings();
            return result with
            {
                LibraryRoot = string.IsNullOrWhiteSpace(result.LibraryRoot) ? paths.Library : result.LibraryRoot
            };
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Text.Json.JsonException)
        {
            return new DesktopSettings { LibraryRoot = paths.Library };
        }
    }

    public async Task SaveAsync(DesktopSettings settings, CancellationToken cancellationToken = default)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(paths.Settings)!);
        var temporary = paths.Settings + ".tmp";
        await using (var stream = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None, 4096, useAsync: true))
        {
            await System.Text.Json.JsonSerializer.SerializeAsync(stream, settings, JsonOptions, cancellationToken);
            await stream.FlushAsync(cancellationToken);
        }
        File.Move(temporary, paths.Settings, overwrite: true);
    }
}
