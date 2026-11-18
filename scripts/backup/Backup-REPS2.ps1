#requires -Version 5.1
<#
.SYNOPSIS
    Creates a verified REPS2 recovery bundle and synchronises the private mirror remote.

.DESCRIPTION
    Produces a dated Git bundle containing every branch, remote-tracking ref and
    tag, verifies it, restores it into a temporary bare repository, and checks
    that the restored object database is intact. Older bundles beyond the
    retention count are removed. The mirror remote is then updated with heads and
    tags only; no remote ref is ever deleted.

    A bundle on the same physical device as the working repository is a fast
    restore layer, not an off-device backup. See docs/BACKUP_AND_RECOVERY.md.

.EXAMPLE
    .\Backup-REPS2.ps1
    .\Backup-REPS2.ps1 -Destination E:\REPS2-Backups -KeepBundles 30
    .\Backup-REPS2.ps1 -RegisterTask
#>
[CmdletBinding()]
param(
    [string]$Repository,
    [string]$Destination = (Join-Path $env:USERPROFILE 'REPS2-Backups'),
    [int]$KeepBundles = 14,
    [string]$MirrorRemote = 'backup-mirror',
    [switch]$SkipMirror,
    [switch]$SkipRestoreTest,
    [switch]$RegisterTask,
    [string]$TaskTime = '02:00'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Optional repository override so the restore test can address a scratch repo.
$script:GitRepoOverride = $null

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $repo = if ($script:GitRepoOverride) { $script:GitRepoOverride } else { $Repository }
    # Native tools (git push, git fetch) write progress to stderr. Merging stderr
    # while $ErrorActionPreference is Stop turns those lines into terminating
    # errors, so relax the preference for the call and judge by exit code.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & git -C $repo @Arguments 2>&1
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) {
        throw "git $($Arguments -join ' ') failed ($code): $($output -join ' ')"
    }
    return $output
}

if ($RegisterTask) {
    $script = $PSCommandPath
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' `
        -Argument "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$script`""
    $trigger = New-ScheduledTaskTrigger -Daily -At $TaskTime
    $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -DontStopOnIdleEnd
    Register-ScheduledTask -TaskName 'REPS2-Backup' -Action $action -Trigger $trigger `
        -Settings $settings -Description 'REPS2 recovery bundle and private mirror sync' -Force | Out-Null
    Write-Output "registered scheduled task REPS2-Backup (daily $TaskTime)"
    exit 0
}

$Repository = if ($Repository) {
    (Resolve-Path $Repository).Path
}
else {
    (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}
if (-not (Test-Path (Join-Path $Repository '.git'))) {
    throw "$Repository is not a Git working tree"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$head = (& git -C $Repository rev-parse HEAD).Trim()
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$bundle = Join-Path $Destination "REPS2-Project-$stamp.bundle"
$log = Join-Path $Destination 'backup-log.jsonl'

$record = [ordered]@{
    kind            = 'reps2.backup.v1'
    started_utc     = (Get-Date).ToUniversalTime().ToString('o')
    repository      = $Repository
    head            = $head
    destination     = $Destination
    bundle          = $null
    bundle_sha256   = $null
    bundle_bytes    = $null
    refs            = 0
    restore_ok      = $false
    mirror_remote   = $null
    mirror_ok       = $false
    pruned_bundles  = 0
    outcome         = 'failed'
    error           = $null
}

try {
    Invoke-Git bundle create $bundle --all | Out-Null
    Invoke-Git bundle verify $bundle | Out-Null

    $record.bundle = $bundle
    $record.bundle_sha256 = (Get-FileHash -Algorithm SHA256 -Path $bundle).Hash
    $record.bundle_bytes = (Get-Item $bundle).Length
    $record.refs = @(Invoke-Git for-each-ref --format='%(refname)').Count

    if (-not $SkipRestoreTest) {
        $scratch = Join-Path ([System.IO.Path]::GetTempPath()) "reps2-restore-$stamp"
        try {
            & git init --quiet --bare $scratch | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "temporary repository creation failed" }
            $script:GitRepoOverride = $scratch
            Invoke-Git fetch --quiet $bundle '+refs/*:refs/*' | Out-Null
            Invoke-Git fsck --full | Out-Null
            $restored = @(Invoke-Git for-each-ref --format='%(refname) %(objectname)')
            $script:GitRepoOverride = $null
            $source = @(Invoke-Git for-each-ref --format='%(refname) %(objectname)')
            $diff = Compare-Object $source $restored
            if ($diff) { throw "restored refs differ from source ($($diff.Count) entries)" }
            $record.restore_ok = $true
        }
        finally {
            $script:GitRepoOverride = $null
            if (Test-Path $scratch) { Remove-Item -Recurse -Force $scratch }
        }
    }

    if (-not $SkipMirror) {
        $remotes = @(Invoke-Git remote)
        if ($remotes -contains $MirrorRemote) {
            $record.mirror_remote = $MirrorRemote
            Invoke-Git push $MirrorRemote '+refs/heads/*:refs/heads/*' '+refs/tags/*:refs/tags/*' | Out-Null
            $record.mirror_ok = $true
        }
        else {
            Write-Warning "mirror remote '$MirrorRemote' is not configured; skipping mirror sync"
        }
    }

    $old = Get-ChildItem -Path $Destination -Filter 'REPS2-Project-*.bundle' |
        Sort-Object LastWriteTime -Descending | Select-Object -Skip $KeepBundles
    foreach ($file in $old) { Remove-Item -Force $file.FullName }
    $record.pruned_bundles = @($old).Count

    $record.outcome = 'succeeded'
    $record | ConvertTo-Json -Compress | Add-Content -Path $log
    Write-Output ("bundle={0}`nsha256={1}`nbytes={2}`nrefs={3}`nrestore_ok={4}`nmirror_ok={5}" -f `
            $record.bundle, $record.bundle_sha256, $record.bundle_bytes, $record.refs,
        $record.restore_ok, $record.mirror_ok)
    exit 0
}
catch {
    $record.error = $_.Exception.Message
    $record | ConvertTo-Json -Compress | Add-Content -Path $log
    Write-Error "backup failed: $($_.Exception.Message)"
    exit 1
}