using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using REPS2.Desktop.ViewModels;

namespace REPS2.Desktop.Pages;

public sealed partial class ProfilePage : Page
{
    public HomeViewModel ViewModel { get; } = new(
        AppServices.Current.LibraryService, () => AppServices.Current.Settings);

    public ProfilePage()
    {
        InitializeComponent();
        Loaded += ProfilePage_Loaded;
    }

    private async void ProfilePage_Loaded(object sender, RoutedEventArgs e) =>
        await ViewModel.RefreshAsync();

    private void OpenLibrary_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.NavigateTo("Library");

    private void OpenTrophy_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.NavigateTo("Trophy");
}
