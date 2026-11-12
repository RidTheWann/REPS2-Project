using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace REPS2.Desktop.Pages;

public sealed partial class DeveloperPage : Page
{
    public DeveloperPage()
    {
        InitializeComponent();
    }

    private async void Capabilities_Click(object sender, RoutedEventArgs e) =>
        await AppServices.Current.RunDeveloperCommandAsync("capabilities");

    private async void Sessions_Click(object sender, RoutedEventArgs e) =>
        await AppServices.Current.RunDeveloperCommandAsync("sessions");

    private async void InspectLatest_Click(object sender, RoutedEventArgs e) =>
        await AppServices.Current.RunDeveloperCommandAsync("inspect latest");

    private async void SummaryLatest_Click(object sender, RoutedEventArgs e) =>
        await AppServices.Current.RunDeveloperCommandAsync("summary latest");

    public void SetCommandOutput(string command, int exitCode, string output)
    {
        OutputHeader.Text = $"reps2 dev {command} · exit {exitCode}";
        OutputText.Text = string.IsNullOrWhiteSpace(output) ? "(no output)" : output;
    }
}
