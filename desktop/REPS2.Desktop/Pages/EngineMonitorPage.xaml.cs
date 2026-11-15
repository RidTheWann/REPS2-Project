using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace REPS2.Desktop.Pages;

public sealed partial class EngineMonitorPage : Page
{
    public EngineMonitorPage()
    {
        InitializeComponent();
    }

    private void OpenDeveloper_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.NavigateTo("Developer");
}
