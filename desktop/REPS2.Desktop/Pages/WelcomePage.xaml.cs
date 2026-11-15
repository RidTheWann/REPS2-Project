using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace REPS2.Desktop.Pages;

public sealed partial class WelcomePage : Page
{
    public WelcomePage()
    {
        InitializeComponent();
    }

    private void ExploreDesktop_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.NavigateTo("Home");

    private void EnterBigPicture_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.EnterBigPicture();

    private void OpenSystem_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.NavigateTo("EngineMonitor");
}
