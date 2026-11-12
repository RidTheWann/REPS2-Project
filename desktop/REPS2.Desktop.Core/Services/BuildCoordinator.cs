using System.Security.Cryptography;
using System.Text.Json;
using REPS2.Desktop.Core.Models;

namespace REPS2.Desktop.Core.Services;

public sealed class BuildCoordinator(DesktopPaths paths, JobStore jobs, SettingsStore settingsStore)
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web) { WriteIndented = true };

    public async Task<NativeBuildRecord> BuildAsync(GameSummary game, CancellationToken cancellationToken = default,
        Func<JobRecord, Task>? onProgress = null)
    {
        if (!game.CanBuild && game.Lifecycle != GameLifecycle.NativeBuilt)
            throw new InvalidOperationException($"A native build is not available from the {game.Lifecycle.Label()} state.");
        if (!File.Exists(game.SourceIsoPath))
            throw new FileNotFoundException("The recorded source ISO is missing.", game.SourceIsoPath);

        var settings = await settingsStore.LoadAsync(cancellationToken);
        var resolution = ToolchainLocator.Resolve(settings, paths);
        if (!resolution.CanBuild)
            throw new InvalidOperationException(string.Join(Environment.NewLine, resolution.Notes));

        var jobId = $"build-{DateTimeOffset.UtcNow:yyyyMMddHHmmssfff}-{Guid.NewGuid():N}";
        var outputRoot = Path.Combine(paths.Builds, game.GameId, jobId);
        var nativeProject = Path.Combine(outputRoot, "boot.native");
        var nativeBuild = Path.Combine(nativeProject, "build");
        var diagnostics = Path.Combine(paths.Logs, "desktop", jobId);
        Directory.CreateDirectory(Path.GetDirectoryName(outputRoot)!);
        Directory.CreateDirectory(diagnostics);

        var record = new JobRecord
        {
            JobId = jobId, GameId = game.GameId, GameName = game.DisplayName,
            JobType = "Native build", Status = "Running", Stage = "Preparing output",
            CreatedAt = DateTimeOffset.UtcNow, UpdatedAt = DateTimeOffset.UtcNow,
            OutputDirectory = outputRoot, DiagnosticsDirectory = diagnostics
        };
        await Publish(record);

        try
        {
            record = await SetStage(record, "ISO analysis and native project generation", "Running the REPS2 frontend.");
            var frontendArgs = new List<string>();
            if (settings.DeveloperDiagnostics)
                frontendArgs.AddRange(["--dev", "--log-dir", diagnostics]);
            frontendArgs.AddRange([
                "recompile-iso", game.SourceIsoPath, outputRoot,
                "--discover", "--tu-count", Math.Clamp(settings.TranslationUnitCount, 1, 16).ToString(),
                "--hle-syscalls"
            ]);
            var frontend = await ProcessRunner.RunAsync(resolution.FrontendPath!, frontendArgs,
                paths.Root, cancellationToken, TimeSpan.FromHours(2));
            if (frontend.ExitCode != 0)
                throw new BuildStepException("REPS2 ISO-to-native preparation failed.", frontend.ExitCode, frontend.CombinedOutput);
            var manifestPath = Path.Combine(outputRoot, "boot.manifest.json");
            if (!File.Exists(manifestPath) || !Directory.Exists(nativeProject))
                throw new BuildStepException("The frontend exited successfully but required native artifacts are missing.", frontend.ExitCode, frontend.CombinedOutput);

            using (var buildManifest = JsonDocument.Parse(await File.ReadAllTextAsync(manifestPath, cancellationToken)))
            {
                var input = buildManifest.RootElement.TryGetProperty("input", out var inputElement) ? inputElement : default;
                var inputSha = input.ValueKind == JsonValueKind.Object && input.TryGetProperty("sha256", out var inputShaElement)
                    ? inputShaElement.GetString() : null;
                if (!string.Equals(inputSha, game.BootElfSha256, StringComparison.OrdinalIgnoreCase))
                    throw new BuildStepException("Generated build manifest does not match the imported boot ELF digest.", frontend.ExitCode, manifestPath);
            }

            record = await SetStage(record, "CMake configure", "Configuring the generated C++20 project.");
            var cmakeArgs = new[]
            {
                "-S", nativeProject, "-B", nativeBuild,
                $"-DREPS2_RUNTIME_INCLUDE={resolution.SdkIncludeRoot}",
                $"-DREPS2_ENGINE_INCLUDE={resolution.SdkIncludeRoot}"
            };
            var configure = await ProcessRunner.RunAsync(resolution.CMakePath!, cmakeArgs, paths.Root,
                cancellationToken, TimeSpan.FromMinutes(20));
            if (configure.ExitCode != 0)
                throw new BuildStepException("CMake configuration failed. Check Visual Studio Build Tools and the Windows SDK.", configure.ExitCode, configure.CombinedOutput);

            record = await SetStage(record, "MSVC Release build", "Compiling and linking the generated native module.");
            var compile = await ProcessRunner.RunAsync(resolution.CMakePath!,
                ["--build", nativeBuild, "--config", "Release", "--parallel"],
                paths.Root, cancellationToken, TimeSpan.FromHours(2));
            if (compile.ExitCode != 0)
                throw new BuildStepException("The native module build failed.", compile.ExitCode, compile.CombinedOutput);

            record = await SetStage(record, "Verifying native artifacts", "Checking the module, build identity and provenance.");
            var identityPath = Directory.EnumerateFiles(nativeBuild,
                "reps2-native-build-identity-Release.json", SearchOption.AllDirectories).FirstOrDefault();
            var modulePathFile = Directory.EnumerateFiles(nativeBuild,
                "reps2-native-module-Release.path", SearchOption.AllDirectories).FirstOrDefault();
            if (identityPath is null || modulePathFile is null)
                throw new BuildStepException("CMake completed but the native build identity or module path file is missing.", 0, compile.CombinedOutput);

            var modulePath = (await File.ReadAllTextAsync(modulePathFile, cancellationToken)).Trim();
            if (!Path.IsPathRooted(modulePath))
                modulePath = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(modulePathFile)!, modulePath));
            if (!IsInside(modulePath, nativeBuild) || !File.Exists(modulePath))
                throw new BuildStepException("The generated native module path is missing or escapes the isolated build directory.", 0, modulePath);

            using var identity = JsonDocument.Parse(await File.ReadAllTextAsync(identityPath, cancellationToken));
            var cacheKey = identity.RootElement.TryGetProperty("cache_key_sha256", out var key)
                ? key.GetString() : null;
            var reportName = identity.RootElement.TryGetProperty("toolchain_report", out var reportElement)
                ? reportElement.GetString() : null;
            if (!IsSha256(cacheKey) || string.IsNullOrWhiteSpace(reportName))
                throw new BuildStepException("The native build identity is missing its cache key or toolchain report reference.", 0, identityPath);

            var reportPath = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(identityPath)!, reportName));
            if (!IsInside(reportPath, nativeBuild) || !File.Exists(reportPath) || !IsInside(identityPath, nativeBuild))
                throw new BuildStepException("The native toolchain report is missing or outside the build directory.", 0, reportPath);

            using var report = JsonDocument.Parse(await File.ReadAllTextAsync(reportPath, cancellationToken));
            var frontendReport = report.RootElement.TryGetProperty("frontend", out var frontendElement) ? frontendElement : default;
            var parentIso = frontendReport.ValueKind == JsonValueKind.Object
                && frontendReport.TryGetProperty("parent_iso_sha256", out var parentElement) ? parentElement.GetString() : null;
            var inputElf = frontendReport.ValueKind == JsonValueKind.Object
                && frontendReport.TryGetProperty("input_elf_sha256", out var elfElement) ? elfElement.GetString() : null;
            if (!string.Equals(parentIso, game.IsoSha256, StringComparison.OrdinalIgnoreCase)
                || !string.Equals(inputElf, game.BootElfSha256, StringComparison.OrdinalIgnoreCase))
                throw new BuildStepException("Native toolchain provenance does not match the imported ISO and boot ELF digests.", 0, reportPath);

            var buildRecord = new NativeBuildRecord
            {
                GameId = game.GameId,
                SourceIsoSha256 = game.IsoSha256,
                BuildDirectory = outputRoot,
                ManifestPath = manifestPath,
                NativeModulePath = modulePath,
                BuildIdentityPath = identityPath,
                CacheKeySha256 = cacheKey!,
                CompletedAt = DateTimeOffset.UtcNow
            };
            var buildRecordPath = Path.Combine(paths.Builds, game.GameId + ".json");
            await WriteAtomicJsonAsync(buildRecordPath, buildRecord, cancellationToken);

            record = record with
            {
                Status = "Succeeded", Stage = "Native-built", Message = "Native module and provenance identity verified.",
                ExitCode = 0, UpdatedAt = DateTimeOffset.UtcNow
            };
            await Publish(record);
            return buildRecord;
        }
        catch (OperationCanceledException)
        {
            record = record with { Status = "Cancelled", Stage = "Cancelled", Message = "The build process tree was stopped.", UpdatedAt = DateTimeOffset.UtcNow };
            await jobs.SaveAsync(record, CancellationToken.None);
            if (onProgress is not null) await onProgress(record);
            throw;
        }
        catch (Exception ex)
        {
            record = record with
            {
                Status = "Failed", Stage = record.Stage,
                Message = ex is BuildStepException step ? $"{step.Message}{Environment.NewLine}{step.Details}" : ex.Message,
                ExitCode = ex is BuildStepException buildStep ? buildStep.ExitCode : null,
                UpdatedAt = DateTimeOffset.UtcNow
            };
            await jobs.SaveAsync(record, CancellationToken.None);
            if (onProgress is not null) await onProgress(record);
            throw;
        }

        async Task<JobRecord> SetStage(JobRecord current, string stage, string? message)
        {
            var next = current with { Stage = stage, Status = "Running", Message = message, UpdatedAt = DateTimeOffset.UtcNow };
            await Publish(next);
            return next;
        }

        async Task Publish(JobRecord current)
        {
            await jobs.SaveAsync(current, cancellationToken);
            if (onProgress is not null) await onProgress(current);
        }
    }

    private static bool IsSha256(string? value) =>
        value is { Length: 64 } && value.All(Uri.IsHexDigit);

    private static bool IsInside(string candidate, string directory)
    {
        if (string.IsNullOrWhiteSpace(candidate) || string.IsNullOrWhiteSpace(directory))
            return false;
        var prefix = Path.GetFullPath(directory) + Path.DirectorySeparatorChar;
        return Path.GetFullPath(candidate).StartsWith(prefix,
            OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
    }

    private static async Task WriteAtomicJsonAsync<T>(string path, T value, CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".tmp";
        await using (var stream = File.Create(temporary))
        {
            await JsonSerializer.SerializeAsync(stream, value, JsonOptions, cancellationToken);
            await stream.FlushAsync(cancellationToken);
        }
        File.Move(temporary, path, overwrite: true);
    }
}

public sealed class BuildStepException(string message, int exitCode, string details) : Exception(message)
{
    public int ExitCode { get; } = exitCode;
    public string Details { get; } = details;
}
