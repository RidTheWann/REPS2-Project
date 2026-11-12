using System.Diagnostics;
using System.IO.Compression;
using System.Security.Cryptography;

namespace REPS2.Desktop.Core.Services;

public sealed record UpdateApplyRequest(
    int WaitForProcessId,
    string InstallDirectory,
    string PackageArchive,
    string ExpectedSha256,
    string ExecutableName,
    string StartupSignalPath,
    TimeSpan? ProcessExitTimeout = null,
    TimeSpan? StartupTimeout = null);

public static class UpdatePackageApplier
{
    private const long MaximumArchiveBytes = 1_500_000_000;
    private const long MaximumExpandedBytes = 3_000_000_000;
    private const long MaximumEntryBytes = 1_000_000_000;
    private const int MaximumEntryCount = 50_000;
    private const string StartupSignalEnvironmentVariable = "REPS2_UPDATE_STARTUP_SIGNAL";

    public static async Task ApplyAsync(UpdateApplyRequest request, CancellationToken cancellationToken = default)
    {
        if (!OperatingSystem.IsWindows())
            throw new PlatformNotSupportedException("REPS2 Desktop self-update is currently supported only on Windows.");
        if (request.WaitForProcessId <= 0)
            throw new ArgumentOutOfRangeException(nameof(request.WaitForProcessId));
        if (!string.Equals(request.ExecutableName, "REPS2.Desktop.exe", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The update executable name is not the expected REPS2 Desktop executable.");
        if (!IsSha256(request.ExpectedSha256))
            throw new InvalidDataException("The expected update checksum is not a valid SHA-256 value.");

        var installDirectory = Path.TrimEndingDirectorySeparator(Path.GetFullPath(request.InstallDirectory));
        var installRoot = Path.GetPathRoot(installDirectory);
        if (string.IsNullOrWhiteSpace(installRoot)
            || string.Equals(installDirectory, Path.TrimEndingDirectorySeparator(installRoot),
                StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The application installation directory cannot be a volume root.");
        var executablePath = Path.Combine(installDirectory, request.ExecutableName);
        if (!File.Exists(executablePath))
            throw new FileNotFoundException("The installed REPS2 Desktop executable was not found.", executablePath);

        var archivePath = Path.GetFullPath(request.PackageArchive);
        if (!File.Exists(archivePath))
            throw new FileNotFoundException("The downloaded REPS2 update package is missing.", archivePath);
        if (new FileInfo(archivePath).Length is <= 0 or > MaximumArchiveBytes)
            throw new InvalidDataException("The update archive size is outside the allowed range.");

        await WaitForTargetProcessAsync(request.WaitForProcessId, executablePath,
            request.ProcessExitTimeout ?? TimeSpan.FromMinutes(2), cancellationToken);

        await using (var stream = File.OpenRead(archivePath))
        {
            var actual = Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken)).ToLowerInvariant();
            if (!string.Equals(actual, request.ExpectedSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The update archive failed SHA-256 verification in the updater process.");
        }

        var parent = Directory.GetParent(installDirectory)?.FullName
            ?? throw new InvalidDataException("The installation directory has no parent directory.");
        var name = Path.GetFileName(installDirectory);
        var transaction = Guid.NewGuid().ToString("N");
        var staging = Path.Combine(parent, $".{name}.update-stage-{transaction}");
        var backup = Path.Combine(parent, $".{name}.update-backup-{transaction}");
        var failed = Path.Combine(parent, $".{name}.update-failed-{transaction}");
        var signal = Path.GetFullPath(request.StartupSignalPath);
        var signalRoot = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "REPS2", "Desktop", "Updates", "Signals");
        EnsureContained(signal, signalRoot);

        Directory.CreateDirectory(staging);
        try
        {
            await ExtractSafelyAsync(archivePath, staging, cancellationToken);
            ValidateApplicationPayload(staging);
            Directory.CreateDirectory(Path.GetDirectoryName(signal)!);
            TryDeleteFile(signal);

            Directory.Move(installDirectory, backup);
            try
            {
                Directory.Move(staging, installDirectory);
            }
            catch
            {
                Directory.Move(backup, installDirectory);
                throw;
            }

            Process? launched = null;
            try
            {
                var start = new ProcessStartInfo
                {
                    FileName = Path.Combine(installDirectory, request.ExecutableName),
                    WorkingDirectory = installDirectory,
                    UseShellExecute = false,
                    CreateNoWindow = true
                };
                start.Environment[StartupSignalEnvironmentVariable] = signal;
                launched = Process.Start(start)
                    ?? throw new InvalidOperationException("Windows did not start the updated REPS2 Desktop process.");

                var startupDeadline = DateTime.UtcNow + (request.StartupTimeout ?? TimeSpan.FromSeconds(60));
                while (DateTime.UtcNow < startupDeadline)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    if (File.Exists(signal))
                    {
                        TryDeleteDirectory(backup);
                        TryDeleteFile(signal);
                        return;
                    }
                    if (launched.HasExited)
                        throw new InvalidOperationException($"The updated application exited before completing startup (exit code {launched.ExitCode}).");
                    await Task.Delay(250, cancellationToken);
                }
                throw new TimeoutException("The updated application did not signal a successful startup within 60 seconds.");
            }
            catch
            {
                if (launched is not null)
                {
                    try
                    {
                        if (!launched.HasExited)
                            launched.Kill(entireProcessTree: true);
                        await launched.WaitForExitAsync(CancellationToken.None);
                    }
                    catch (InvalidOperationException) { }
                    catch (System.ComponentModel.Win32Exception) { }
                }
                if (Directory.Exists(installDirectory))
                    Directory.Move(installDirectory, failed);
                if (Directory.Exists(backup))
                    Directory.Move(backup, installDirectory);
                if (Directory.Exists(failed))
                    TryDeleteDirectory(failed);
                TryDeleteFile(signal);

                var rollbackExecutable = Path.Combine(installDirectory, request.ExecutableName);
                if (File.Exists(rollbackExecutable))
                {
                    try
                    {
                        Process.Start(new ProcessStartInfo
                        {
                            FileName = rollbackExecutable,
                            WorkingDirectory = installDirectory,
                            UseShellExecute = true
                        });
                    }
                    catch (System.ComponentModel.Win32Exception) { }
                }
                throw;
            }
            finally
            {
                launched?.Dispose();
            }
        }
        finally
        {
            if (Directory.Exists(staging))
                TryDeleteDirectory(staging);
        }
    }

    public static async Task ExtractSafelyAsync(
        string archivePath,
        string destinationDirectory,
        CancellationToken cancellationToken = default)
    {
        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(destinationDirectory)) + Path.DirectorySeparatorChar;
        using var archive = ZipFile.OpenRead(archivePath);
        if (archive.Entries.Count is <= 0 or > MaximumEntryCount)
            throw new InvalidDataException("The update archive contains an invalid number of entries.");

        var destinations = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        long totalExpanded = 0;
        foreach (var entry in archive.Entries)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var normalized = entry.FullName.Replace('\\', '/');
            if (string.IsNullOrWhiteSpace(normalized) || normalized.StartsWith("/", StringComparison.Ordinal)
                || normalized.Contains(':'))
                throw new InvalidDataException($"The update archive contains an unsafe path: {entry.FullName}");

            var segments = normalized.Split('/', StringSplitOptions.RemoveEmptyEntries);
            if (segments.Length == 0 || segments.Any(segment => segment is "." or ".." || !IsSafeWindowsPathSegment(segment)))
                throw new InvalidDataException($"The update archive contains a path traversal entry: {entry.FullName}");

            var relativePath = Path.Combine(segments);
            var destination = Path.GetFullPath(Path.Combine(destinationDirectory, relativePath));
            if (!destination.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"The update archive entry escapes its staging directory: {entry.FullName}");

            // Unix file type 0120000 represents a symbolic link. Do not create links from update packages.
            var unixFileType = (entry.ExternalAttributes >> 16) & 0xF000;
            if (unixFileType == 0xA000)
                throw new InvalidDataException($"The update archive contains a symbolic link: {entry.FullName}");

            var isDirectory = normalized.EndsWith("/", StringComparison.Ordinal);
            var identity = Path.TrimEndingDirectorySeparator(destination);
            if (!destinations.Add(identity))
                throw new InvalidDataException($"The update archive contains a duplicate path: {entry.FullName}");

            if (isDirectory)
            {
                Directory.CreateDirectory(destination);
                continue;
            }

            if (entry.Length < 0 || entry.Length > MaximumEntryBytes)
                throw new InvalidDataException($"The update archive entry is too large: {entry.FullName}");
            totalExpanded = checked(totalExpanded + entry.Length);
            if (totalExpanded > MaximumExpandedBytes)
                throw new InvalidDataException("The update archive exceeds the maximum expanded size.");

            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            await using var input = entry.Open();
            await using var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                128 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan);
            var buffer = new byte[128 * 1024];
            long written = 0;
            int read;
            while ((read = await input.ReadAsync(buffer, cancellationToken)) > 0)
            {
                written += read;
                if (written > entry.Length || written > MaximumEntryBytes)
                    throw new InvalidDataException($"The update archive entry exceeded its declared size: {entry.FullName}");
                await output.WriteAsync(buffer.AsMemory(0, read), cancellationToken);
            }
            if (written != entry.Length)
                throw new InvalidDataException($"The update archive entry was truncated: {entry.FullName}");
        }
    }

    private static async Task WaitForTargetProcessAsync(
        int processId, string expectedExecutable, TimeSpan timeout, CancellationToken cancellationToken)
    {
        Process? target;
        try
        {
            target = Process.GetProcessById(processId);
        }
        catch (ArgumentException)
        {
            return; // The application has already exited.
        }

        using (target)
        {
            if (target.HasExited)
                return;

            string? actualPath;
            try { actualPath = target.MainModule?.FileName; }
            catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
            {
                if (target.HasExited)
                    return;
                throw new InvalidOperationException("The updater could not verify the target application process.", ex);
            }

            if (actualPath is null || !string.Equals(Path.GetFullPath(actualPath), Path.GetFullPath(expectedExecutable),
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("The updater process ID does not belong to the installed REPS2 Desktop executable.");

            if (target.HasExited)
                return;
            using var timeoutCts = new CancellationTokenSource(timeout);
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeoutCts.Token);
            try
            {
                await target.WaitForExitAsync(linked.Token);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                throw new TimeoutException("REPS2 Desktop did not close in time for the update.");
            }
        }
    }

    private static void ValidateApplicationPayload(string staging)
    {
        var requiredFiles = new[]
        {
            "REPS2.Desktop.exe",
            Path.Combine("tools", "bin", "reps2.exe"),
            Path.Combine("tools", "bin", "rg-reps2-engine.exe"),
            Path.Combine("tools", "include", "reps2", "runtime", "abi_version.hpp"),
            Path.Combine("updater", "REPS2.Desktop.Updater.exe")
        };
        foreach (var relative in requiredFiles)
        {
            var path = Path.Combine(staging, relative);
            if (!File.Exists(path))
                throw new InvalidDataException($"The update package is missing a required application file: {relative}");
        }
    }

    private static void EnsureContained(string path, string root)
    {
        var fullRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)) + Path.DirectorySeparatorChar;
        var fullPath = Path.GetFullPath(path);
        if (!fullPath.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)
            || !string.Equals(Path.GetExtension(fullPath), ".ok", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The updater startup signal path is outside the application data update directory.");
    }

    private static bool IsSafeWindowsPathSegment(string segment)
    {
        if (string.IsNullOrWhiteSpace(segment) || segment.EndsWith(' ') || segment.EndsWith('.'))
            return false;
        if (segment.Any(character => character < 32 || "<>:\"|?*".Contains(character)))
            return false;
        var deviceName = segment.Split('.')[0];
        if (deviceName.Equals("CON", StringComparison.OrdinalIgnoreCase)
            || deviceName.Equals("PRN", StringComparison.OrdinalIgnoreCase)
            || deviceName.Equals("AUX", StringComparison.OrdinalIgnoreCase)
            || deviceName.Equals("NUL", StringComparison.OrdinalIgnoreCase))
            return false;
        if (deviceName.Length == 4
            && (deviceName.StartsWith("COM", StringComparison.OrdinalIgnoreCase)
                || deviceName.StartsWith("LPT", StringComparison.OrdinalIgnoreCase))
            && deviceName[3] is >= '1' and <= '9')
            return false;
        return true;
    }

    private static bool IsSha256(string value) =>
        value.Length == 64 && value.All(Uri.IsHexDigit);

    private static void TryDeleteFile(string path)
    {
        try { if (File.Exists(path)) File.Delete(path); }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    private static void TryDeleteDirectory(string path)
    {
        try { if (Directory.Exists(path)) Directory.Delete(path, recursive: true); }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }
}
