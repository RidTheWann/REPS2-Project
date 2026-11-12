using System.Runtime.InteropServices;
using REPS2.Desktop.Core.Services;

namespace REPS2.Desktop.Updater;

internal static partial class Program
{
    private static async Task<int> Main(string[] args)
    {
        try
        {
            var values = ParseArguments(args);
            var request = new UpdateApplyRequest(
                int.Parse(Required(values, "--wait-pid"), System.Globalization.CultureInfo.InvariantCulture),
                Required(values, "--install-dir"),
                Required(values, "--archive"),
                Required(values, "--sha256"),
                Required(values, "--exe"),
                Required(values, "--startup-signal"));
            await UpdatePackageApplier.ApplyAsync(request);
            return 0;
        }
        catch (Exception exception)
        {
            WriteLog(exception);
            ShowError("REPS2 update could not be applied.\n\n" + exception.Message);
            return 1;
        }
    }

    private static Dictionary<string, string> ParseArguments(string[] args)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var index = 0; index < args.Length; index++)
        {
            if (!args[index].StartsWith("--", StringComparison.Ordinal) || index + 1 >= args.Length)
                throw new ArgumentException($"Invalid updater argument: {args[index]}");
            if (!result.TryAdd(args[index], args[++index]))
                throw new ArgumentException($"Duplicate updater argument: {args[index - 1]}");
        }
        return result;
    }

    private static string Required(IReadOnlyDictionary<string, string> values, string name) =>
        values.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"Required updater argument is missing: {name}");

    private static void WriteLog(Exception exception)
    {
        try
        {
            var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            var directory = Path.Combine(local, "REPS2", "Logs");
            Directory.CreateDirectory(directory);
            File.AppendAllText(Path.Combine(directory, "desktop-updater.log"),
                $"[{DateTimeOffset.UtcNow:O}] {exception}{Environment.NewLine}{Environment.NewLine}");
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    private static void ShowError(string message)
    {
        try { _ = MessageBoxW(IntPtr.Zero, message, "REPS2 Desktop Updater", 0x10 | 0x1000); }
        catch (DllNotFoundException) { }
        catch (EntryPointNotFoundException) { }
    }

    [LibraryImport("user32.dll", EntryPoint = "MessageBoxW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial int MessageBoxW(IntPtr window, string text, string caption, uint type);
}
