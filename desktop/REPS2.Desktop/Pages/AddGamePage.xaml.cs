using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace REPS2.Desktop.Pages;

public sealed partial class AddGamePage : Page
{
    public AddGamePage()
    {
        InitializeComponent();
    }

    internal void SetImporting(bool isImporting)
    {
        BrowseButton.IsEnabled = !isImporting;
        ImportProgressRing.IsActive = isImporting;
        ImportStatusPanel.Visibility = isImporting ? Visibility.Visible : Visibility.Collapsed;
    }

    internal void SetImportError(
        string message,
        string title = "The image could not be inspected",
        string guidance = "Choose a raw PS2 ISO with 2048-byte sectors. Compressed images and incomplete copies cannot be inspected here.")
    {
        ImportSelectionPanel.Visibility = Visibility.Collapsed;
        ImportErrorTitle.Text = title;
        ImportErrorDescription.Text = message;
        ImportErrorGuidance.Text = guidance;
        ImportErrorPanel.Visibility = Visibility.Visible;
    }

    internal void ClearImportError()
    {
        ImportErrorDescription.Text = string.Empty;
        ImportErrorPanel.Visibility = Visibility.Collapsed;
        ImportSelectionPanel.Visibility = Visibility.Visible;
    }

    private async void Browse_Click(object sender, RoutedEventArgs e) =>
        await AppServices.Current.AddGameAsync();

    private async void RetryImport_Click(object sender, RoutedEventArgs e)
    {
        ClearImportError();
        await AppServices.Current.AddGameAsync();
    }

    private void ViewDiagnostics_Click(object sender, RoutedEventArgs e) =>
        AppServices.Current.NavigateTo("Developer");
}
