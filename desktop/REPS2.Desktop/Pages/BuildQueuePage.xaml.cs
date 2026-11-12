using System.Collections.ObjectModel;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using REPS2.Desktop.Core.Models;

namespace REPS2.Desktop.Pages;

public sealed partial class BuildQueuePage : Page
{
    private readonly ObservableCollection<JobRecord> _jobs = [];
    private bool _subscribed;

    public BuildQueuePage()
    {
        InitializeComponent();
        JobsList.ItemsSource = _jobs;
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
        await RefreshAsync();
    }

    private void BuildQueuePage_Unloaded(object sender, RoutedEventArgs e)
    {
        if (_subscribed)
        {
            AppServices.Current.JobUpdated -= OnJobUpdated;
            _subscribed = false;
        }
    }

    public async Task RefreshAsync()
    {
        try
        {
            var jobs = await AppServices.Current.JobStore.LoadRecentAsync();
            _jobs.Clear();
            foreach (var job in jobs)
                _jobs.Add(job);
            UpdateSummary();
            EmptyJobs.Visibility = _jobs.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
            JobsList.Visibility = _jobs.Count == 0 ? Visibility.Collapsed : Visibility.Visible;
        }
        catch (Exception ex)
        {
            QueueSummary.Text = $"Build history could not be loaded: {ex.Message}";
            AppServices.Current.SetStatus("Build history could not be loaded · see local diagnostics");
        }
    }

    private void OnJobUpdated(JobRecord job)
    {
        DispatcherQueue.TryEnqueue(() => Upsert(job));
    }

    private void Upsert(JobRecord job)
    {
        var index = -1;
        for (var i = 0; i < _jobs.Count; i++)
        {
            if (string.Equals(_jobs[i].JobId, job.JobId, StringComparison.Ordinal))
            {
                index = i;
                break;
            }
        }

        if (index >= 0)
            _jobs[index] = job;
        else
            _jobs.Insert(0, job);

        UpdateSummary();
        EmptyJobs.Visibility = _jobs.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        JobsList.Visibility = _jobs.Count == 0 ? Visibility.Collapsed : Visibility.Visible;
    }

    private void UpdateSummary()
    {
        var running = _jobs.Count(job => string.Equals(job.Status, "Running", StringComparison.OrdinalIgnoreCase));
        QueueSummary.Text = $"{_jobs.Count} recent job records · {running} running · updates persisted locally";
    }

    private void OpenDirectory_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: string path } || string.IsNullOrWhiteSpace(path))
            return;

        try
        {
            var fullPath = Path.GetFullPath(path);
            var root = Path.GetFullPath(AppServices.Current.Paths.Root);
            var relative = Path.GetRelativePath(root, fullPath);
            var outsideRoot = Path.IsPathRooted(relative)
                || relative == ".."
                || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
                || relative.StartsWith(".." + Path.AltDirectorySeparatorChar, StringComparison.Ordinal);
            if (outsideRoot)
                throw new InvalidOperationException("The job directory is outside the REPS2 application data root.");

            if (!Directory.Exists(fullPath))
                throw new DirectoryNotFoundException($"The recorded job directory is no longer available: {fullPath}");

            Process.Start(new ProcessStartInfo { FileName = fullPath, UseShellExecute = true });
        }
        catch (Exception ex)
        {
            AppServices.Current.SetStatus($"Cannot open job directory · {ex.Message}");
        }
    }

    private async void Refresh_Click(object sender, RoutedEventArgs e) => await RefreshAsync();
}
