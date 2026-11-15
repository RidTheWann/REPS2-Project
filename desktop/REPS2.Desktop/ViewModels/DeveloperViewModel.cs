namespace REPS2.Desktop.ViewModels;

public sealed class DeveloperViewModel : ViewModelBase
{
    private string _header = "No CLI result";
    private string _output = string.Empty;
    private bool _hasOutput;

    public string Header { get => _header; private set => Set(ref _header, value); }
    public string Output { get => _output; private set => Set(ref _output, value); }
    public bool HasOutput { get => _hasOutput; private set => Set(ref _hasOutput, value); }
    public bool LogsEmptyVisible => !HasOutput;

    public void SetOutput(string command, int exitCode, string output)
    {
        Header = $"reps2 dev {command} · exit {exitCode}";
        Output = string.IsNullOrWhiteSpace(output) ? "(no output)" : output;
        HasOutput = true;
        OnPropertyChanged(nameof(LogsEmptyVisible));
    }
}
