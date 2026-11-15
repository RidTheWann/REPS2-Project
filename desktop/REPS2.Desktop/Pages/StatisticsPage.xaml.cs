using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using REPS2.Desktop.ViewModels;

namespace REPS2.Desktop.Pages;

public sealed partial class StatisticsPage : Page
{
    public HomeViewModel ViewModel { get; } = new(
        AppServices.Current.LibraryService, () => AppServices.Current.Settings);

    public StatisticsPage()
    {
        InitializeComponent();
        Loaded += StatisticsPage_Loaded;
    }

    private async void StatisticsPage_Loaded(object sender, RoutedEventArgs e) =>
        await ViewModel.RefreshAsync();
}
