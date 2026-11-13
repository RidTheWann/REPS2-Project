using System.Collections.ObjectModel;
using REPS2.Desktop.Api.Contracts;
using REPS2.Desktop.Core.Services;

namespace REPS2.Desktop.ViewModels;

public sealed class BuildQueueViewModel(JobStore jobs) : ViewModelBase
{
    private string _summary = "No jobs recorded";
    private bool _emptyVisible = true;
    private bool _jobsVisible;

    public ObservableCollection<JobRecord> Jobs { get; } = [];
    public string Summary { get => _summary; private set => Set(ref _summary, value); }
    public bool EmptyVisible { get => _emptyVisible; private set => Set(ref _emptyVisible, value); }
    public bool JobsVisible { get => _jobsVisible; private set => Set(ref _jobsVisible, value); }

    public async Task RefreshAsync()
    {
        try
        {
            var recent = await jobs.LoadRecentAsync();
            Jobs.Clear();
            foreach (var job in recent)
                Jobs.Add(job);
            UpdateState();
        }
        catch (Exception ex)
        {
            Summary = $"Build history could not be loaded: {ex.Message}";
        }
    }

    public void Upsert(JobRecord job)
    {
        var index = -1;
        for (var i = 0; i < Jobs.Count; i++)
        {
            if (string.Equals(Jobs[i].JobId, job.JobId, StringComparison.Ordinal))
            {
                index = i;
                break;
            }
        }

        if (index >= 0)
            Jobs[index] = job;
        else
            Jobs.Insert(0, job);
        UpdateState();
    }

    private void UpdateState()
    {
        var running = Jobs.Count(job => string.Equals(job.Status, "Running", StringComparison.OrdinalIgnoreCase));
        Summary = $"{Jobs.Count} recent job records · {running} running · updates persisted locally";
        EmptyVisible = Jobs.Count == 0;
        JobsVisible = Jobs.Count > 0;
    }
}
