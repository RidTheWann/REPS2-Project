using System.Text.Json;
using REPS2.Desktop.Api.Contracts;

namespace REPS2.Desktop.Core.Services;

public sealed class JobStore(DesktopPaths paths)
{
    private static readonly JsonSerializerOptions Options = new(JsonSerializerDefaults.Web) { WriteIndented = true };

    public async Task SaveAsync(JobRecord record, CancellationToken cancellationToken = default)
    {
        Directory.CreateDirectory(paths.Jobs);
        var path = Path.Combine(paths.Jobs, record.JobId + ".json");
        var temporary = path + ".tmp";
        await using (var stream = File.Create(temporary))
        {
            await JsonSerializer.SerializeAsync(stream, record, Options, cancellationToken);
            await stream.FlushAsync(cancellationToken);
        }
        File.Move(temporary, path, overwrite: true);
    }

    public async Task<IReadOnlyList<JobRecord>> LoadRecentAsync(CancellationToken cancellationToken = default)
    {
        Directory.CreateDirectory(paths.Jobs);
        var result = new List<JobRecord>();
        foreach (var file in Directory.EnumerateFiles(paths.Jobs, "*.json").OrderByDescending(File.GetLastWriteTimeUtc).Take(100))
        {
            cancellationToken.ThrowIfCancellationRequested();
            try
            {
                await using var stream = File.OpenRead(file);
                var record = await JsonSerializer.DeserializeAsync<JobRecord>(stream, Options, cancellationToken);
                if (record is not null && record.Kind == "reps2.desktop.job.v1")
                    result.Add(record);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
            {
                // One damaged job record must not hide the rest of the history.
            }
        }
        return result.OrderByDescending(item => item.UpdatedAt).ToArray();
    }
}
