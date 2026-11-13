using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using REPS2.Desktop.ViewModels;

namespace REPS2.Desktop.Pages;

public sealed partial class DeveloperPage : Page
{
    public DeveloperViewModel ViewModel { get; } = new();

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
}
