namespace REPS2.Desktop.Core.Services;

public sealed record ToolchainResolution(
    string? FrontendPath,
    string? EngineRunnerPath,
    string? SdkIncludeRoot,
    string? CMakePath,
    IReadOnlyList<string> Notes)
{
    public bool CanImport => FrontendPath is not null;
    public bool CanBuild => FrontendPath is not null && SdkIncludeRoot is not null && CMakePath is not null;
}

public static class ToolchainLocator
{
    public static ToolchainResolution Resolve(DesktopSettings settings, DesktopPaths paths, string? applicationBase = null)
    {
        var basePath = applicationBase ?? AppContext.BaseDirectory;
        var notes = new List<string>();
        var sdkEnvironment = Environment.GetEnvironmentVariable("REPS2_SDK_ROOT") ?? string.Empty;
        var sdkEnvInclude = string.IsNullOrWhiteSpace(sdkEnvironment)
            ? string.Empty : Path.Combine(sdkEnvironment, "include");
        var frontendCandidates = new[]
        {
            settings.FrontendPath,
            Path.Combine(basePath, "tools", "bin", "reps2.exe"),
            Path.Combine(basePath, "tools", "reps2.exe"),
            Environment.GetEnvironmentVariable("REPS2_FRONTEND_PATH") ?? string.Empty,
            Path.Combine(paths.Toolchain, "bin", "reps2.exe"),
            Path.Combine(paths.Root, "bin", "reps2.exe")
        };
        var runnerCandidates = new[]
        {
            settings.EngineRunnerPath,
            Path.Combine(basePath, "tools", "bin", "rg-reps2-engine.exe"),
            Path.Combine(paths.Toolchain, "bin", "rg-reps2-engine.exe"),
            Path.Combine(paths.Root, "bin", "rg-reps2-engine.exe")
        };

        var frontend = FirstFile(frontendCandidates);
        var runner = FirstFile(runnerCandidates);
        var includeCandidates = new[]
        {
            settings.SdkIncludeRoot,
            Path.Combine(basePath, "tools", "include"),
            sdkEnvInclude,
            Path.Combine(paths.Toolchain, "include"),
            Path.Combine(paths.Root, "include")
        };
        var include = includeCandidates
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Select(Path.GetFullPath)
            .FirstOrDefault(IsSdkIncludeRoot);

        var cmakeEnvironment = Environment.GetEnvironmentVariable("CMAKE_COMMAND");
        var cmake = FirstFile(new[]
        {
            settings.CMakePath,
            cmakeEnvironment,
            FindOnPath("cmake.exe"),
            FindOnPath("cmake")
        });

        if (frontend is null)
            notes.Add("reps2.exe was not found. Install the REPS2 Windows SDK bundle or set the frontend executable path in Settings.");
        if (runner is null)
            notes.Add("rg-reps2-engine.exe was not found. Bounded execution probes are unavailable.");
        if (include is null)
            notes.Add("The REPS2 public runtime/engine headers were not found. Configure the SDK include root before native builds.");
        if (cmake is null)
            notes.Add("CMake was not found. Native module builds require CMake and a configured MSVC C++ toolchain.");

        return new ToolchainResolution(frontend, runner, include, cmake, notes);
    }

    private static string? FirstFile(IEnumerable<string?> candidates) =>
        candidates.Where(value => !string.IsNullOrWhiteSpace(value))
            .Select(value => value!)
            .Select(value => value.Contains(Path.DirectorySeparatorChar) || value.Contains(Path.AltDirectorySeparatorChar)
                ? Path.GetFullPath(value) : FindOnPath(value))
            .FirstOrDefault(value => value is not null && File.Exists(value));

    private static bool IsSdkIncludeRoot(string root) =>
        File.Exists(Path.Combine(root, "reps2", "runtime", "abi_version.hpp"))
        && Directory.Exists(Path.Combine(root, "rg_reps2_engine"));

    private static string? FindOnPath(string? executable)
    {
        if (string.IsNullOrWhiteSpace(executable))
            return null;
        var suffixes = OperatingSystem.IsWindows() && !executable.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)
            ? new[] { executable, executable + ".exe" }
            : new[] { executable };
        foreach (var directory in (Environment.GetEnvironmentVariable("PATH") ?? string.Empty)
                     .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
        foreach (var candidate in suffixes)
        {
            var path = Path.Combine(directory.Trim('"'), candidate);
            if (File.Exists(path))
                return path;
        }
        return null;
    }
}
