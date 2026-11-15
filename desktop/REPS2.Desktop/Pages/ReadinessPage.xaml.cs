using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.ViewModels;

namespace REPS2.Desktop.Pages;

public sealed partial class ReadinessPage : Page
{
    public ReadinessViewModel ViewModel { get; } = new();

    public ReadinessPage()
    {
        InitializeComponent();
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        if (e.Parameter is GameSummary game)
            ViewModel.Load(game);
    }

    private void ViewDetails_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.OpenGame(ViewModel.Game);

    private void OpenDeveloper_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.NavigateTo("Developer");

    private void ReturnHome_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.NavigateTo("Home");
}
