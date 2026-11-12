using System.Globalization;
using System.Net;
using System.Net.Http.Headers;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text;
using System.Text.RegularExpressions;

namespace REPS2.Desktop.Core.Services;

public sealed record DesktopReleaseInfo(
    string TagName,
    string Version,
    string ReleaseName,
    string ReleaseNotes,
    Uri ReleasePage,
    Uri PackageUri,
    Uri ChecksumUri,
    long PackageSize,
    string? PublishedAt,
    string? ApiDigest);

public sealed record UpdateCheckResult(
    string CurrentVersion,
    DesktopReleaseInfo? AvailableRelease,
    string? LatestPublishedVersion,
    string Message)
{
    public bool IsUpdateAvailable => AvailableRelease is not null;
}

public sealed record UpdatePackageDownload(string PackagePath, string Sha256);

public sealed record UpdateDownloadProgress(long BytesReceived, long? TotalBytes)
{
    public double? Fraction => TotalBytes is > 0 ? Math.Clamp((double)BytesReceived / TotalBytes.Value, 0, 1) : null;
}

public sealed class UpdateService
{
    public const string PackageFileName = "REPS2-Desktop-Alpha-windows-x64.zip";
    public const string ChecksumFileName = PackageFileName + ".sha256";
    private const string ApiUrl = "https://api.github.com/repos/RidTheWann/REPS2-Project/releases?per_page=100";
    private const long MaximumPackageBytes = 1_500_000_000;

    private static readonly HttpClient Http = CreateHttpClient();

    public static string CurrentAppVersion()
    {
        var value = Assembly.GetEntryAssembly()?
            .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
        if (string.IsNullOrWhiteSpace(value))
            value = Assembly.GetEntryAssembly()?.GetName().Version?.ToString() ?? "0.1.0-alpha.0";
        return value.Split('+', 2)[0];
    }

    public async Task<UpdateCheckResult> CheckForUpdatesAsync(
        string? currentVersion = null,
        CancellationToken cancellationToken = default)
    {
        currentVersion ??= CurrentAppVersion();
        if (!DesktopSemanticVersion.TryParse(currentVersion, out var installed))
            throw new InvalidOperationException($"The installed REPS2 version '{currentVersion}' is not valid semantic version metadata.");

        using var checkTimeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        checkTimeout.CancelAfter(TimeSpan.FromSeconds(15));
        using var response = await Http.GetAsync(ApiUrl, HttpCompletionOption.ResponseHeadersRead, checkTimeout.Token);
        if (response.StatusCode == HttpStatusCode.NotFound)
            return new UpdateCheckResult(currentVersion, null, null, "The REPS2 Alpha update feed is not published yet.");
        response.EnsureSuccessStatusCode();

        await using var responseStream = await response.Content.ReadAsStreamAsync(checkTimeout.Token);
        using var json = await JsonDocument.ParseAsync(responseStream, cancellationToken: checkTimeout.Token);
        if (json.RootElement.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("The GitHub release response was not a JSON array.");

        DesktopReleaseInfo? newest = null;
        DesktopSemanticVersion newestVersion = default;
        foreach (var release in json.RootElement.EnumerateArray())
        {
            if (ReadBool(release, "draft"))
                continue;

            var tag = ReadString(release, "tag_name");
            if (tag is null || !tag.StartsWith("desktop-v", StringComparison.Ordinal))
                continue;
            var versionText = tag["desktop-v".Length..];
            if (!DesktopSemanticVersion.TryParse(versionText, out var candidateVersion))
                continue;
            if (newest is not null && candidateVersion.CompareTo(newestVersion) <= 0)
                continue;

            var assets = Get(release, "assets");
            if (assets.ValueKind != JsonValueKind.Array)
                continue;
            var package = FindAsset(assets, PackageFileName);
            var checksum = FindAsset(assets, ChecksumFileName);
            if (package is null || checksum is null || package.Size <= 0 || package.Size > MaximumPackageBytes)
                continue;
            if (!IsOwnedReleaseAsset(package.Url) || !IsOwnedReleaseAsset(checksum.Url))
                continue;

            var releaseUrl = ReadUri(release, "html_url");
            if (releaseUrl is null || releaseUrl.Host != "github.com")
                continue;

            newest = new DesktopReleaseInfo(
                tag,
                versionText,
                ReadString(release, "name") ?? $"REPS2 Desktop {versionText}",
                ReadString(release, "body") ?? string.Empty,
                releaseUrl,
                package.Url,
                checksum.Url,
                package.Size,
                ReadString(release, "published_at"),
                package.Digest);
            newestVersion = candidateVersion;
        }

        if (newest is null)
            return new UpdateCheckResult(currentVersion, null, null, "No published REPS2 Desktop Alpha release was found.");

        var comparison = newestVersion.CompareTo(installed);
        return comparison > 0
            ? new UpdateCheckResult(currentVersion, newest, newest.Version, $"REPS2 Desktop {newest.Version} is available.")
            : new UpdateCheckResult(currentVersion, null, newest.Version, "REPS2 Desktop is up to date.");
    }

    public async Task<UpdatePackageDownload> DownloadAndVerifyAsync(
        DesktopReleaseInfo release,
        string updateDirectory,
        IProgress<UpdateDownloadProgress>? progress = null,
        CancellationToken cancellationToken = default)
    {
        Directory.CreateDirectory(updateDirectory);
        var safeVersion = string.Concat(release.Version.Select(character =>
            char.IsAsciiLetterOrDigit(character) || character is '.' or '-' ? character : '_'));
        var destination = Path.Combine(updateDirectory, $"REPS2-Desktop-{safeVersion}.zip");
        var temporary = destination + ".download";

        using var checksumResponse = await Http.GetAsync(release.ChecksumUri,
            HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        checksumResponse.EnsureSuccessStatusCode();
        if (checksumResponse.Content.Headers.ContentLength is > 4096)
            throw new InvalidDataException("The release checksum sidecar is unexpectedly large.");
        await using var checksumStream = await checksumResponse.Content.ReadAsStreamAsync(cancellationToken);
        using var checksumBuffer = new MemoryStream();
        var checksumBytes = new byte[512];
        int checksumRead;
        while ((checksumRead = await checksumStream.ReadAsync(checksumBytes, cancellationToken)) > 0)
        {
            if (checksumBuffer.Length + checksumRead > 4096)
                throw new InvalidDataException("The release checksum sidecar is unexpectedly large.");
            await checksumBuffer.WriteAsync(checksumBytes.AsMemory(0, checksumRead), cancellationToken);
        }
        var checksumText = Encoding.UTF8.GetString(checksumBuffer.ToArray());
        var expectedSha256 = ParseChecksum(checksumText, PackageFileName);
        if (release.ApiDigest is { Length: > 0 } apiDigest)
        {
            const string prefix = "sha256:";
            if (!apiDigest.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)
                || !string.Equals(apiDigest[prefix.Length..], expectedSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The GitHub release asset digest does not match its SHA-256 sidecar.");
        }

        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Get, release.PackageUri);
            using var response = await Http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            response.EnsureSuccessStatusCode();
            var total = response.Content.Headers.ContentLength ?? release.PackageSize;
            if (total <= 0 || total > MaximumPackageBytes)
                throw new InvalidDataException($"The update package size is outside the permitted range ({total} bytes).");

            await using var input = await response.Content.ReadAsStreamAsync(cancellationToken);
            await using (var output = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None,
                             128 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan))
            {
                var buffer = new byte[128 * 1024];
                long received = 0;
                int read;
                while ((read = await input.ReadAsync(buffer, cancellationToken)) > 0)
                {
                    received += read;
                    if (received > MaximumPackageBytes || received > total + 1_048_576)
                        throw new InvalidDataException("The update package exceeded its declared download size.");
                    await output.WriteAsync(buffer.AsMemory(0, read), cancellationToken);
                    progress?.Report(new UpdateDownloadProgress(received, total));
                }
                await output.FlushAsync(cancellationToken);
                if (received != total)
                    throw new EndOfStreamException($"The update package download was incomplete ({received} of {total} bytes).");
            }

            await using var verifyStream = File.OpenRead(temporary);
            var actualSha256 = Convert.ToHexString(await SHA256.HashDataAsync(verifyStream, cancellationToken)).ToLowerInvariant();
            if (!string.Equals(actualSha256, expectedSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The downloaded update failed SHA-256 verification. The package was discarded.");

            File.Move(temporary, destination, overwrite: true);
            return new UpdatePackageDownload(destination, expectedSha256);
        }
        catch
        {
            TryDelete(temporary);
            throw;
        }
    }

    private static HttpClient CreateHttpClient()
    {
        var client = new HttpClient { Timeout = TimeSpan.FromMinutes(30) };
        client.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("REPS2-Desktop", "0.1"));
        client.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
        client.DefaultRequestHeaders.Add("X-GitHub-Api-Version", "2022-11-28");
        return client;
    }

    private static string ParseChecksum(string text, string requiredFileName)
    {
        foreach (var rawLine in text.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries))
        {
            var fields = rawLine.Trim().Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
            if (fields.Length == 0 || !Regex.IsMatch(fields[0], "^[0-9a-fA-F]{64}$", RegexOptions.CultureInvariant))
                continue;
            if (fields.Length > 1)
            {
                var fileName = fields[^1].TrimStart('*');
                if (!string.Equals(Path.GetFileName(fileName), requiredFileName, StringComparison.Ordinal))
                    continue;
            }
            return fields[0].ToLowerInvariant();
        }
        throw new InvalidDataException($"The release checksum sidecar does not contain a SHA-256 for {requiredFileName}.");
    }

    private static ReleaseAsset? FindAsset(JsonElement assets, string name)
    {
        foreach (var asset in assets.EnumerateArray())
        {
            if (!string.Equals(ReadString(asset, "name"), name, StringComparison.Ordinal))
                continue;
            var url = ReadUri(asset, "browser_download_url");
            var size = ReadLong(asset, "size");
            if (url is null || size < 0)
                return null;
            return new ReleaseAsset(url, size, ReadString(asset, "digest"));
        }
        return null;
    }

    private static bool IsOwnedReleaseAsset(Uri uri) =>
        uri.IsAbsoluteUri && uri.Scheme == Uri.UriSchemeHttps && uri.Host.Equals("github.com", StringComparison.OrdinalIgnoreCase)
        && uri.AbsolutePath.StartsWith("/RidTheWann/REPS2-Project/releases/download/", StringComparison.OrdinalIgnoreCase);

    private static Uri? ReadUri(JsonElement element, string name)
    {
        var value = ReadString(element, name);
        return Uri.TryCreate(value, UriKind.Absolute, out var uri) ? uri : null;
    }

    private static JsonElement Get(JsonElement element, string name) =>
        element.ValueKind == JsonValueKind.Object && element.TryGetProperty(name, out var value) ? value : default;

    private static string? ReadString(JsonElement element, string name)
    {
        var value = Get(element, name);
        return value.ValueKind == JsonValueKind.String ? value.GetString() : null;
    }

    private static long ReadLong(JsonElement element, string name)
    {
        var value = Get(element, name);
        return value.ValueKind == JsonValueKind.Number && value.TryGetInt64(out var result) ? result : 0;
    }

    private static bool ReadBool(JsonElement element, string name) => Get(element, name).ValueKind == JsonValueKind.True;

    private static void TryDelete(string path)
    {
        try { if (File.Exists(path)) File.Delete(path); }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    private sealed record ReleaseAsset(Uri Url, long Size, string? Digest);
}

public readonly record struct DesktopSemanticVersion(
    int Major,
    int Minor,
    int Patch,
    string? Prerelease) : IComparable<DesktopSemanticVersion>
{
    private static readonly Regex Pattern = new(
        @"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$",
        RegexOptions.CultureInvariant | RegexOptions.Compiled);

    public static bool TryParse(string? value, out DesktopSemanticVersion version)
    {
        version = default;
        if (string.IsNullOrWhiteSpace(value))
            return false;
        var normalized = value.Split('+', 2)[0];
        var match = Pattern.Match(normalized);
        if (!match.Success
            || !int.TryParse(match.Groups[1].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var major)
            || !int.TryParse(match.Groups[2].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var minor)
            || !int.TryParse(match.Groups[3].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var patch))
            return false;
        version = new DesktopSemanticVersion(major, minor, patch,
            match.Groups[4].Success ? match.Groups[4].Value : null);
        return true;
    }

    public int CompareTo(DesktopSemanticVersion other)
    {
        var result = Major.CompareTo(other.Major);
        if (result != 0) return result;
        result = Minor.CompareTo(other.Minor);
        if (result != 0) return result;
        result = Patch.CompareTo(other.Patch);
        if (result != 0) return result;
        if (Prerelease is null) return other.Prerelease is null ? 0 : 1;
        if (other.Prerelease is null) return -1;

        var left = Prerelease.Split('.');
        var right = other.Prerelease.Split('.');
        for (var index = 0; index < Math.Min(left.Length, right.Length); index++)
        {
            var leftNumeric = long.TryParse(left[index], NumberStyles.None, CultureInfo.InvariantCulture, out var leftNumber);
            var rightNumeric = long.TryParse(right[index], NumberStyles.None, CultureInfo.InvariantCulture, out var rightNumber);
            if (leftNumeric && rightNumeric)
                result = leftNumber.CompareTo(rightNumber);
            else if (leftNumeric != rightNumeric)
                result = leftNumeric ? -1 : 1;
            else
                result = string.Compare(left[index], right[index], StringComparison.Ordinal);
            if (result != 0) return result;
        }
        return left.Length.CompareTo(right.Length);
    }

    public override string ToString() =>
        $"{Major}.{Minor}.{Patch}{(Prerelease is null ? string.Empty : "-" + Prerelease)}";
}
