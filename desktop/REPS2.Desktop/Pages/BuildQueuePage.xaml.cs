using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.ViewModels;

namespace REPS2.Desktop.Pages;

public sealed partial class BuildQueuePage : Page
{
    private bool _subscribed;

    public BuildQueueViewModel ViewModel { get; } = new(AppServices.Current.JobStore);

    public BuildQueuePage()
    {
        InitializeComponent();
        Loaded += BuildQueuePage_Loaded;
        Unloaded += BuildQueuePage_Unloaded;
    }

    private async void BuildQueuePage_Loaded(object sender, RoutedEventArgs e)
    {
        if (!_subscribed)
        {
            AppServices.Current.JobUpdated += OnJobUpdated;
            _subscribed = true;
        }
        await ViewModel.RefreshAsync();
    }

    private void BuildQueuePage_Unloaded(object sender, RoutedEventArgs e)
    {
        if (_subscribed)
        {
            AppServices.Current.JobUpdated -= OnJobUpdated;
            _subscribed = false;
        }
    }

    public Task RefreshAsync() => ViewModel.RefreshAsync();

    private void OnJobUpdated(JobRecord job)
    {
        DispatcherQueue.TryEnqueue(() => ViewModel.Upsert(job));
    }

    private void OpenDirectory_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: string path } || string.IsNullOrWhiteSpace(path))
            return;

        try
        {
            AppServices.Current.HostShell.OpenDirectory(path);
        }
        catch (Exception ex)
        {
            AppServices.Current.SetStatus($"Cannot open job directory · {ex.Message}");
        }
    }

    private async void Refresh_Click(object sender, RoutedEventArgs e) => await RefreshAsync();
}
