using System.Diagnostics;
using System.Text;

namespace REPS2.Desktop.Api.Ipc;

public sealed record ProcessResult(int ExitCode, string StandardOutput, string StandardError, TimeSpan Duration)
{
    public string CombinedOutput => string.Join(Environment.NewLine,
        new[] { StandardOutput.Trim(), StandardError.Trim() }.Where(value => value.Length > 0));
}

/// <summary>Local Windows process transport used by the IPC layer.</summary>
public static class ProcessRunner
{
    public static async Task<ProcessResult> RunAsync(
        string executable,
        IEnumerable<string> arguments,
        string workingDirectory,
        CancellationToken cancellationToken = default,
        TimeSpan? timeout = null)
    {
        if (!File.Exists(executable) && !IsPathCommand(executable))
            throw new FileNotFoundException($"Executable was not found: {executable}", executable);

        var start = new ProcessStartInfo
        {
            FileName = executable,
            WorkingDirectory = Directory.Exists(workingDirectory) ? workingDirectory : Environment.CurrentDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            StandardOutputEncoding = new UTF8Encoding(false),
            StandardErrorEncoding = new UTF8Encoding(false)
        };
        foreach (var argument in arguments)
            start.ArgumentList.Add(argument);

        using var process = new Process { StartInfo = start, EnableRaisingEvents = true };
        var stopwatch = Stopwatch.StartNew();
        var started = false;
        try
        {
            started = process.Start();
            if (!started)
                throw new InvalidOperationException($"Failed to start {executable}.");

            using var timeoutCts = timeout is { } duration ? new CancellationTokenSource(duration) : null;
            using var linkedCts = timeoutCts is null
                ? CancellationTokenSource.CreateLinkedTokenSource(cancellationToken)
                : CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeoutCts.Token);

            var stdoutTask = process.StandardOutput.ReadToEndAsync(linkedCts.Token);
            var stderrTask = process.StandardError.ReadToEndAsync(linkedCts.Token);
            try
            {
                await process.WaitForExitAsync(linkedCts.Token);
            }
            catch (OperationCanceledException)
            {
                TryKillTree(process);
                throw;
            }

            var stdout = await stdoutTask;
            var stderr = await stderrTask;
            stopwatch.Stop();
            return new ProcessResult(process.ExitCode, Trim(stdout), Trim(stderr), stopwatch.Elapsed);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException($"{Path.GetFileName(executable)} exceeded its configured execution timeout.");
        }
        finally
        {
            if (started && !process.HasExited)
                TryKillTree(process);
        }
    }

    private static string Trim(string value)
    {
        const int maximum = 1_000_000;
        return value.Length <= maximum
            ? value
            : value[..maximum] + Environment.NewLine + "[output truncated by REPS2 Desktop]";
    }

    private static void TryKillTree(Process process)
    {
        try { process.Kill(entireProcessTree: true); }
        catch (InvalidOperationException) { }
        catch (System.ComponentModel.Win32Exception) { }
    }

    private static bool IsPathCommand(string executable) =>
        !Path.IsPathRooted(executable) && !executable.Contains(Path.DirectorySeparatorChar)
        && !executable.Contains(Path.AltDirectorySeparatorChar);
}
