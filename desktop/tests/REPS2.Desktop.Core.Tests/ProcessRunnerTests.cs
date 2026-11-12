using System.Diagnostics;
using REPS2.Desktop.Core.Services;
using Xunit;

namespace REPS2.Desktop.Core.Tests;

public sealed class ProcessRunnerTests
{
    [Fact]
    public async Task CancellationStopsLongRunningChildProcess()
    {
        var (executable, arguments) = OperatingSystem.IsWindows()
            ? ("cmd.exe", new[] { "/c", "ping -n 30 127.0.0.1 > NUL" })
            : ("/bin/sh", new[] { "-c", "sleep 30" });

        using var cancellation = new CancellationTokenSource(TimeSpan.FromMilliseconds(700));
        var stopwatch = Stopwatch.StartNew();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            ProcessRunner.RunAsync(
                executable,
                arguments,
                Environment.CurrentDirectory,
                cancellation.Token,
                TimeSpan.FromSeconds(10)));

        stopwatch.Stop();
        Assert.True(stopwatch.Elapsed < TimeSpan.FromSeconds(8),
            $"The child process took too long to stop after cancellation: {stopwatch.Elapsed}.");
    }
}
