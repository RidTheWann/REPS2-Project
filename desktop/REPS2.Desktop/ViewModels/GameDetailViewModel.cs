using REPS2.Desktop.Api.Contracts;

namespace REPS2.Desktop.ViewModels;

public sealed class GameDetailViewModel : ViewModelBase
{
    private GameSummary? _game;
    private string _title = string.Empty;
    private string _serialLine = string.Empty;
    private string _lifecycleText = string.Empty;
    private string _evidenceText = string.Empty;
    private string _recoveryText = string.Empty;
    private string _isoPath = string.Empty;
    private string _isoDigest = string.Empty;
    private string _entryPoint = string.Empty;
    private string _blocksValue = string.Empty;
    private string _instructionsValue = string.Empty;
    private string _unsupportedLine = string.Empty;
    private string _buildStateText = string.Empty;
    private string _buildCacheKey = string.Empty;
    private string _buildModulePath = string.Empty;
    private bool _relinkVisible;
    private bool _buildVisible;
    private bool _probeVisible;
    private bool _buildEnabled;
    private bool _probeEnabled;

    public GameSummary? Game => _game;
    public string Title { get => _title; private set => Set(ref _title, value); }
    public string SerialLine { get => _serialLine; private set => Set(ref _serialLine, value); }
    public string LifecycleText { get => _lifecycleText; private set => Set(ref _lifecycleText, value); }
    public string EvidenceText { get => _evidenceText; private set => Set(ref _evidenceText, value); }
    public string RecoveryText { get => _recoveryText; private set => Set(ref _recoveryText, value); }
    public string IsoPath { get => _isoPath; private set => Set(ref _isoPath, value); }
    public string IsoDigest { get => _isoDigest; private set => Set(ref _isoDigest, value); }
    public string EntryPoint { get => _entryPoint; private set => Set(ref _entryPoint, value); }
    public string BlocksValue { get => _blocksValue; private set => Set(ref _blocksValue, value); }
    public string InstructionsValue { get => _instructionsValue; private set => Set(ref _instructionsValue, value); }
    public string UnsupportedLine { get => _unsupportedLine; private set => Set(ref _unsupportedLine, value); }
    public string BuildStateText { get => _buildStateText; private set => Set(ref _buildStateText, value); }
    public string BuildCacheKey { get => _buildCacheKey; private set => Set(ref _buildCacheKey, value); }
    public string BuildModulePath { get => _buildModulePath; private set => Set(ref _buildModulePath, value); }
    public bool RelinkVisible { get => _relinkVisible; private set => Set(ref _relinkVisible, value); }
    public bool BuildVisible { get => _buildVisible; private set => Set(ref _buildVisible, value); }
    public bool ProbeVisible { get => _probeVisible; private set => Set(ref _probeVisible, value); }
    public bool BuildEnabled { get => _buildEnabled; private set => Set(ref _buildEnabled, value); }
    public bool ProbeEnabled { get => _probeEnabled; private set => Set(ref _probeEnabled, value); }

    public void Load(GameSummary game)
    {
        _game = game;
        Title = game.DisplayName;
        SerialLine = $"{game.Subtitle}   ·   {game.LifecycleLabel}";
        LifecycleText = game.LifecycleLabel;
        EvidenceText = game.EvidenceNote;
        RecoveryText = game.RecoveryReason ?? game.AnalysisError ?? string.Empty;
        IsoPath = game.SourceIsoPath;
        IsoDigest = game.IsoSha256;
        EntryPoint = game.BootEntryPoint ?? "Not recorded";
        BlocksValue = game.BasicBlockCount.ToString("N0");
        InstructionsValue = game.DecodedInstructionCount.ToString("N0");
        UnsupportedLine = $"Unsupported blocks: {game.UnsupportedBlockCount:N0}   ·   unresolved indirect: {game.UnresolvedIndirectCount:N0}";
        BuildStateText = game.Lifecycle is GameLifecycle.NativeBuilt or GameLifecycle.ExecutionBlocked
            ? "Native module and build identity verified"
            : "No verified native module";
        BuildCacheKey = game.CacheKeySha256 is null ? "Cache key: not available" : $"Cache key: {game.CacheKeySha256}";
        BuildModulePath = game.NativeModulePath ?? "Native module path not recorded";
        RelinkVisible = game.CanRelinkSourceIso;
        BuildVisible = game.CanBuild;
        ProbeVisible = game.CanRunProbe;
        BuildEnabled = game.CanBuild;
        ProbeEnabled = game.CanRunProbe;
    }
}
