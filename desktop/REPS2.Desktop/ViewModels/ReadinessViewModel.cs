using REPS2.Desktop.Api.Contracts;

namespace REPS2.Desktop.ViewModels;

public sealed class ReadinessViewModel : ViewModelBase
{
    private GameSummary _game = new();
    private string _sourceImage = "No source image reference recorded";
    private string _nativeModule = "No verified artifact";
    private string _bootEvidence = "Not verified";

    public GameSummary Game { get => _game; private set => Set(ref _game, value); }
    public string Title => Game.DisplayName;
    public string SerialLine => Game.Subtitle;
    public string Lifecycle => Game.LifecycleLabel;
    public string SourcePolicy => "Original ISO stays external";
    public string SourceImage { get => _sourceImage; private set => Set(ref _sourceImage, value); }
    public string NativeModule { get => _nativeModule; private set => Set(ref _nativeModule, value); }
    public string BootEvidence { get => _bootEvidence; private set => Set(ref _bootEvidence, value); }

    public void Load(GameSummary game)
    {
        Game = game;
        SourceImage = game.Lifecycle == GameLifecycle.RecoveryRequired
            ? "Source ISO requires attention"
            : string.IsNullOrWhiteSpace(game.SourceIsoPath)
                ? "Owner-supplied ISO required"
                : "Owner-supplied ISO reference recorded";
        NativeModule = game.Lifecycle is GameLifecycle.NativeBuilt
            or GameLifecycle.ExecutionBlocked
            or GameLifecycle.BootTested
            or GameLifecycle.Playable
                ? "Native module and build identity verified"
                : "No verified artifact";
        BootEvidence = game.Lifecycle is GameLifecycle.BootTested or GameLifecycle.Playable
            ? game.EvidenceNote
            : "Not verified";

        OnPropertyChanged(nameof(Title));
        OnPropertyChanged(nameof(SerialLine));
        OnPropertyChanged(nameof(Lifecycle));
    }
}
