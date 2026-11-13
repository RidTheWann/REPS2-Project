using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;
using REPS2.Desktop.Api.Contracts;

namespace REPS2.Desktop.Core.Services;

public static partial class CompatibilityStateDeriver
{
    private const long EeMainRamBytes = 32L * 1024L * 1024L;
    private static readonly JsonDocumentOptions DocumentOptions = new()
    {
        AllowTrailingCommas = false,
        CommentHandling = JsonCommentHandling.Disallow,
        MaxDepth = 32
    };

    [GeneratedRegex("^[0-9a-fA-F]{64}$", RegexOptions.CultureInvariant)]
    private static partial Regex Sha256Pattern();

    public static GameSummary Parse(string manifestPath, string? buildRecordPath = null)
    {
        var gameDirectory = Path.GetDirectoryName(Path.GetFullPath(manifestPath))!;
        using var document = JsonDocument.Parse(File.ReadAllText(manifestPath), DocumentOptions);
        var root = document.RootElement;

        var schema = ReadInt(root, "schema_version");
        var gameId = ReadString(root, "game_id") ?? Path.GetFileName(gameDirectory);
        var displayName = ReadString(root, "display_name") ?? "Unknown PS2 title";
        var serial = ReadString(root, "serial_candidate");
        var source = Get(root, "source_iso");
        var boot = Get(root, "boot");
        var analysis = Get(root, "analysis");
        var native = Get(root, "native_build");
        var artifacts = Get(root, "artifacts");

        var isoPath = ReadString(source, "path") ?? string.Empty;
        var isoSha = ReadString(source, "sha256") ?? string.Empty;
        var bootSha = ReadString(boot, "sha256") ?? string.Empty;
        var bootElfName = ReadString(artifacts, "boot_elf") ?? "boot.elf";
        var bootElfPath = SafeChildPath(gameDirectory, bootElfName);
        var lifecycle = GameLifecycle.Unknown;
        string? recovery = null;

        if (schema != 1 || !Sha256Pattern().IsMatch(isoSha) || !Sha256Pattern().IsMatch(bootSha))
        {
            recovery = "The library manifest is invalid or does not contain valid ISO and boot-ELF SHA-256 identities.";
            lifecycle = GameLifecycle.RecoveryRequired;
        }
        else if (string.IsNullOrWhiteSpace(isoPath) || !File.Exists(isoPath))
        {
            recovery = "The original ISO is not available at the recorded path. Relink support must verify the same SHA-256 before updating this record.";
            lifecycle = GameLifecycle.RecoveryRequired;
        }
        else if (ReadLong(source, "size_bytes") > 0
                 && new FileInfo(isoPath).Length != ReadLong(source, "size_bytes"))
        {
            recovery = "The source ISO file size differs from the imported record. Re-import/relink must verify its original SHA-256.";
            lifecycle = GameLifecycle.RecoveryRequired;
        }
        else if (string.IsNullOrWhiteSpace(bootElfPath) || !File.Exists(bootElfPath))
        {
            recovery = "The extracted boot ELF is missing from this library entry.";
            lifecycle = GameLifecycle.RecoveryRequired;
        }
        else
        {
            var actualBootSha = HashFile(bootElfPath);
            if (!string.Equals(actualBootSha, bootSha, StringComparison.OrdinalIgnoreCase))
            {
                recovery = "The extracted boot ELF no longer matches its recorded SHA-256.";
                lifecycle = GameLifecycle.RecoveryRequired;
            }
            else
            {
                lifecycle = GameLifecycle.Imported;
                var analysisStatus = ReadString(analysis, "status");
                if (string.Equals(analysisStatus, "failed", StringComparison.OrdinalIgnoreCase))
                    lifecycle = GameLifecycle.AnalysisFailed;
                else if (string.Equals(analysisStatus, "completed", StringComparison.OrdinalIgnoreCase)
                         && ReadLong(analysis, "basic_block_count") > 0
                         && ReadLong(analysis, "decoded_instruction_count") > 0)
                    lifecycle = GameLifecycle.Analyzed;

                if (lifecycle == GameLifecycle.Analyzed)
                {
                    var generatedCpp = ReadBool(native, "generated_cpp");
                    var generatedRam = ReadBool(native, "generated_ram_image");
                    var cppName = ReadString(artifacts, "generated_cpp");
                    var ramName = ReadString(artifacts, "initial_ee_ram");
                    var cppPath = string.IsNullOrWhiteSpace(cppName) ? string.Empty : SafeChildPath(gameDirectory, cppName);
                    var ramPath = string.IsNullOrWhiteSpace(ramName) ? string.Empty : SafeChildPath(gameDirectory, ramName);
                    var cppExists = !string.IsNullOrWhiteSpace(cppPath) && File.Exists(cppPath);
                    var ramExists = !string.IsNullOrWhiteSpace(ramPath) && File.Exists(ramPath)
                        && new FileInfo(ramPath).Length == EeMainRamBytes;

                    if (generatedCpp && generatedRam && cppExists && ramExists)
                        lifecycle = GameLifecycle.Translated;
                    else
                        lifecycle = GameLifecycle.TranslationIncomplete;
                }
            }
        }

        var build = ReadBuildRecord(buildRecordPath, gameId, isoSha);
        if (lifecycle == GameLifecycle.Translated && build is not null
            && IsInside(build.NativeModulePath, build.BuildDirectory)
            && IsInside(build.BuildIdentityPath, build.BuildDirectory)
            && File.Exists(build.NativeModulePath) && File.Exists(build.BuildIdentityPath))
        {
            try
            {
                using var identity = JsonDocument.Parse(File.ReadAllText(build.BuildIdentityPath), DocumentOptions);
                var key = ReadString(identity.RootElement, "cache_key_sha256");
                if (Sha256Pattern().IsMatch(key ?? string.Empty)
                    && string.Equals(key, build.CacheKeySha256, StringComparison.OrdinalIgnoreCase))
                    lifecycle = GameLifecycle.NativeBuilt;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
            {
                // Missing or malformed build identity never upgrades a game's state.
            }
        }

        return new GameSummary
        {
            GameId = gameId,
            DisplayName = displayName,
            SerialCandidate = serial,
            GameDirectory = gameDirectory,
            SourceIsoPath = isoPath,
            IsoSha256 = isoSha,
            BootElfPath = bootElfPath,
            BootElfSha256 = bootSha,
            VolumeIdentifier = ReadString(Get(root, "disc"), "volume_identifier"),
            BootEntryPoint = ReadString(Get(boot, "elf"), "entry_point"),
            Lifecycle = lifecycle,
            BuildDirectory = build?.BuildDirectory,
            NativeModulePath = build?.NativeModulePath,
            BuildIdentityPath = build?.BuildIdentityPath,
            CacheKeySha256 = build?.CacheKeySha256,
            RecoveryReason = recovery,
            AnalysisError = ReadString(analysis, "error") ?? ReadString(native, "codegen_error"),
            BasicBlockCount = ReadLong(analysis, "basic_block_count"),
            DecodedInstructionCount = ReadLong(analysis, "decoded_instruction_count"),
            UnsupportedBlockCount = ReadLong(analysis, "unsupported_block_count"),
            UnresolvedIndirectCount = ReadLong(analysis, "unresolved_indirect_count")
        };
    }

    private static string HashFile(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    private static NativeBuildRecord? ReadBuildRecord(string? path, string gameId, string isoSha)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            return null;
        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(path), DocumentOptions);
            var root = document.RootElement;
            if (ReadString(root, "kind") != "reps2.desktop.native_build.v1"
                || ReadInt(root, "schema_version") != 1
                || ReadString(root, "game_id") != gameId
                || !string.Equals(ReadString(root, "source_iso_sha256"), isoSha, StringComparison.OrdinalIgnoreCase))
                return null;
            return JsonSerializer.Deserialize<NativeBuildRecord>(root.GetRawText(),
                new JsonSerializerOptions(JsonSerializerDefaults.Web));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }
    }

    private static bool IsInside(string candidate, string directory)
    {
        if (string.IsNullOrWhiteSpace(candidate) || string.IsNullOrWhiteSpace(directory))
            return false;
        var root = Path.GetFullPath(directory) + Path.DirectorySeparatorChar;
        var path = Path.GetFullPath(candidate);
        return path.StartsWith(root, OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
    }

    private static string SafeChildPath(string directory, string relative)
    {
        if (Path.IsPathRooted(relative))
            return string.Empty;
        var full = Path.GetFullPath(Path.Combine(directory, relative));
        var prefix = Path.GetFullPath(directory) + Path.DirectorySeparatorChar;
        return full.StartsWith(prefix, OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal)
            ? full
            : string.Empty;
    }

    private static JsonElement Get(JsonElement element, string name) =>
        element.ValueKind == JsonValueKind.Object && element.TryGetProperty(name, out var value)
            ? value : default;

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

    private static int ReadInt(JsonElement element, string name)
    {
        var value = Get(element, name);
        return value.ValueKind == JsonValueKind.Number && value.TryGetInt32(out var result) ? result : 0;
    }

    private static bool ReadBool(JsonElement element, string name)
    {
        var value = Get(element, name);
        return value.ValueKind == JsonValueKind.True;
    }
}
