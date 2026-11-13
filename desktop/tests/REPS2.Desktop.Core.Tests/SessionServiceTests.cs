using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.Core.Services;
using Xunit;

namespace REPS2.Desktop.Core.Tests;

public sealed class SessionServiceTests
{
    private const string ValidReport = """
        {
          "kind": "rg.reps2.engine.run_report.v1",
          "outcome": "instruction_budget_exhausted",
          "session": {
            "pc_hex": "0x800761A4",
            "instructions_executed": 3890274
          }
        }
        """;

    [Fact]
    public async Task ParsesAValidRunReportAndForwardsTheRequest()
    {
        using var fixture = new ProbeFixture(ValidReport);
        var service = new SessionService(fixture.Client);

        var report = await service.RunBoundedProbeAsync(fixture.Request());

        Assert.Equal(ContractVersions.EngineRunReportKind, report.Kind);
        Assert.Equal("instruction_budget_exhausted", report.Outcome);
        Assert.Equal("0x800761A4", report.ProgramCounterHex);
        Assert.Equal(3890274, report.InstructionsExecuted);
        Assert.Equal("/reports/report.json", report.ReportPath);

        var forwarded = Assert.Single(fixture.Client.ProbeRequests);
        Assert.Equal("C:/library/games/game-1/build/boot.native/module.dll", forwarded.NativeModulePath);
        Assert.Equal(Path.Combine(fixture.Root, "boot.ram.bin"), forwarded.RamImagePath);
        Assert.Equal("C:/library/source.iso", forwarded.SourceIsoPath);
        Assert.Equal(5_000_000, forwarded.InstructionBudget);
    }

    [Fact]
    public async Task RejectsAReportWithAnUnexpectedKind()
    {
        using var fixture = new ProbeFixture("""{ "kind": "something.else", "outcome": "ok" }""");
        var service = new SessionService(fixture.Client);

        var exception = await Assert.ThrowsAsync<InvalidDataException>(
            () => service.RunBoundedProbeAsync(fixture.Request()));
        Assert.Contains("run-report", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task RejectsMalformedReportJson()
    {
        using var fixture = new ProbeFixture("not json");
        var service = new SessionService(fixture.Client);

        await Assert.ThrowsAsync<InvalidDataException>(() => service.RunBoundedProbeAsync(fixture.Request()));
    }

    [Fact]
    public async Task SurfacesANonZeroRunnerExitWithTheProbeResult()
    {
        using var fixture = new ProbeFixture(ValidReport, exitCode: 3);
        var service = new SessionService(fixture.Client);

        var exception = await Assert.ThrowsAsync<ProbeExecutionException>(
            () => service.RunBoundedProbeAsync(fixture.Request()));
        Assert.Equal(3, exception.Probe.Invocation.ExitCode);
        Assert.Equal("/reports/report.json", exception.Probe.ReportPath);
    }

    [Fact]
    public async Task RequiresTheInitialRamImage()
    {
        using var fixture = new ProbeFixture(ValidReport, createRamImage: false);
        var service = new SessionService(fixture.Client);

        await Assert.ThrowsAsync<FileNotFoundException>(() => service.RunBoundedProbeAsync(fixture.Request()));
    }

    [Fact]
    public async Task RefusesAProbeOutsideTheNativeBuiltLifecycle()
    {
        using var fixture = new ProbeFixture(ValidReport);
        var request = fixture.Request();
        request = request with { Game = request.Game with { Lifecycle = GameLifecycle.Translated } };
        var service = new SessionService(fixture.Client);

        await Assert.ThrowsAsync<InvalidOperationException>(() => service.RunBoundedProbeAsync(request));
    }

    private sealed class FakeToolchainClient : IToolchainClient
    {
        public List<RunProbeRequest> ProbeRequests { get; } = [];
        private ProbeInvocationResult? _result;

        public void Return(ProbeInvocationResult result) => _result = result;

        public string ContractVersion => ContractVersions.Toolchain;

        public Task<ToolInvocationResult> ImportGameAsync(
            string frontendPath, ImportGameRequest request, CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public Task<ToolInvocationResult> RecompileIsoAsync(
            string frontendPath, RecompileIsoRequest request, CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public Task<ToolInvocationResult> RunDeveloperCommandAsync(
            string frontendPath, string command, string logDirectory, CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public Task<ProbeInvocationResult> RunProbeAsync(
            string engineRunnerPath, RunProbeRequest request, string reportDirectory,
            CancellationToken cancellationToken = default)
        {
            ProbeRequests.Add(request);
            return Task.FromResult(_result!);
        }
    }

    private sealed class ProbeFixture : IDisposable
    {
        private readonly string _root;
        private readonly ProbeInvocationResult _result;
        private readonly GameSummary _game;

        public ProbeFixture(string reportText, int exitCode = 0, bool createRamImage = true)
        {
            _root = Path.Combine(Path.GetTempPath(), "reps2-session-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(_root);
            if (createRamImage)
                File.WriteAllBytes(Path.Combine(_root, "boot.ram.bin"), [0]);

            _result = new ProbeInvocationResult(
                "/reports/report.json",
                reportText,
                new ToolInvocationResult(ContractVersions.Toolchain, exitCode, string.Empty, string.Empty,
                    TimeSpan.Zero));
            _game = new GameSummary
            {
                GameId = "game-1",
                DisplayName = "Fixture",
                SourceIsoPath = "C:/library/source.iso",
                Lifecycle = GameLifecycle.NativeBuilt,
                BuildDirectory = _root,
                NativeModulePath = "C:/library/games/game-1/build/boot.native/module.dll"
            };

            Client = new FakeToolchainClient();
            Client.Return(_result);
        }

        public string Root => _root;

        public FakeToolchainClient Client { get; }

        public BoundedProbeRequest Request() =>
            new(_game, "C:/tools/rg-reps2-engine.exe", "C:/app", "/reports", 5_000_000);

        public void Dispose()
        {
            try
            {
                if (Directory.Exists(_root))
                    Directory.Delete(_root, recursive: true);
            }
            catch (IOException)
            {
            }
        }
    }
}
