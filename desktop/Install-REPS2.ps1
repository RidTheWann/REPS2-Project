$ErrorActionPreference = 'Stop'

$source = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$installRoot = Join-Path $env:LOCALAPPDATA 'Programs\REPS2'
$shortcutRoot = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$shortcutPath = Join-Path $shortcutRoot 'REPS2 Desktop.lnk'
$exe = Join-Path $installRoot 'REPS2.Desktop.exe'

if (-not (Test-Path -LiteralPath (Join-Path $source 'REPS2.Desktop.exe') -PathType Leaf)) {
    throw 'REPS2.Desktop.exe was not found beside this installer script. Extract the full Desktop Alpha ZIP first.'
}
if (-not (Test-Path -LiteralPath (Join-Path $source 'updater\REPS2.Desktop.Updater.exe') -PathType Leaf)) {
    throw 'The self-contained REPS2 updater helper is missing from this package.'
}
function Ensure-VcRuntime {
    $runtimeKey = 'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64'
    $runtime = Get-ItemProperty -LiteralPath $runtimeKey -ErrorAction SilentlyContinue
    if ($runtime.Installed -eq 1) {
        return
    }

    $runtimeInstaller = Join-Path $source 'prerequisites\vc_redist.x64.exe'
    if (-not (Test-Path -LiteralPath $runtimeInstaller -PathType Leaf)) {
        throw 'The Microsoft Visual C++ x64 Redistributable is missing from this package.'
    }

    Write-Host 'Installing the Microsoft Visual C++ x64 runtime prerequisite...' -ForegroundColor Yellow
    $process = Start-Process -FilePath $runtimeInstaller -ArgumentList '/install', '/quiet', '/norestart' -Verb RunAs -Wait -PassThru
    if ($process.ExitCode -notin @(0, 1638, 3010)) {
        throw "Microsoft Visual C++ Runtime installation failed with exit code $($process.ExitCode)."
    }

    $runtime = Get-ItemProperty -LiteralPath $runtimeKey -ErrorAction SilentlyContinue
    if ($runtime.Installed -ne 1) {
        throw 'Microsoft Visual C++ x64 Runtime is still unavailable after installation.'
    }
}

Ensure-VcRuntime

if (Test-Path -LiteralPath $installRoot) {
    $running = Get-Process -Name 'REPS2.Desktop' -ErrorAction SilentlyContinue
    if ($running) {
        throw 'REPS2 Desktop is already running. Close it before running the first-time installer.'
    }
}

New-Item -ItemType Directory -Path (Split-Path -Parent $installRoot) -Force | Out-Null
$staging = "$installRoot.installing-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $staging -Force | Out-Null
try {
    Copy-Item -Path (Join-Path $source '*') -Destination $staging -Recurse -Force
    if (-not (Test-Path -LiteralPath (Join-Path $staging 'tools\bin\reps2.exe') -PathType Leaf)) {
        throw 'The REPS2 frontend is missing from the package. Installation was cancelled.'
    }

    if (Test-Path -LiteralPath $installRoot) {
        $backup = "$installRoot.backup-$([guid]::NewGuid().ToString('N'))"
        Move-Item -LiteralPath $installRoot -Destination $backup
        try {
            Move-Item -LiteralPath $staging -Destination $installRoot
        }
        catch {
            Move-Item -LiteralPath $backup -Destination $installRoot
            throw
        }
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    }
    else {
        Move-Item -LiteralPath $staging -Destination $installRoot
    }

    New-Item -ItemType Directory -Path $shortcutRoot -Force | Out-Null
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = $installRoot
    $shortcut.Description = 'REPS2 Desktop Alpha'
    $shortcut.Save()

    Write-Host "REPS2 Desktop installed to: $installRoot" -ForegroundColor Green
    Write-Host 'Future Alpha releases can be installed from inside REPS2 with one click.' -ForegroundColor Green
    Start-Process -FilePath $exe -WorkingDirectory $installRoot
}
finally {
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    }
}
