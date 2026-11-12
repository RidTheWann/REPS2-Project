# SPDX-License-Identifier: GPL-3.0-or-later
# Assemble the self-contained Windows x64 Desktop Alpha package and setup executable.
param(
    [Parameter(Mandatory)][string]$PublishDir,
    [Parameter(Mandatory)][string]$SdkDir,
    [Parameter(Mandatory)][string]$OutputDir,
    [string]$VcRedistUrl = "https://aka.ms/vs/17/release/vc_redist.x64.exe",
    [switch]$SkipInstaller
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$publish = (Resolve-Path -LiteralPath $PublishDir).Path
$sdk = (Resolve-Path -LiteralPath $SdkDir).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDir).Path

$required = @(
    (Join-Path $publish "REPS2.Desktop.exe"),
    (Join-Path $publish "updater\REPS2.Desktop.Updater.exe"),
    (Join-Path $sdk "bin\reps2.exe"),
    (Join-Path $sdk "bin\rg-reps2-engine.exe")
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required package input is missing: $path"
    }
}

$csproj = [xml](Get-Content -LiteralPath (Join-Path $repoRoot "desktop\REPS2.Desktop\REPS2.Desktop.csproj"))
$properties = $csproj.Project.PropertyGroup | Where-Object { $_.Version } | Select-Object -First 1
$appVersion = [string]$properties.Version
$appFileVersion = [string]$properties.FileVersion
if (-not $appVersion -or $appFileVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') {
    throw "Cannot read Version/FileVersion from the desktop project."
}

$staging = Join-Path $output "package-staging"
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Path $staging -Force | Out-Null

Copy-Item -Path (Join-Path $publish "*") -Destination $staging -Recurse -Force
$tools = Join-Path $staging "tools"
New-Item -ItemType Directory -Path $tools -Force | Out-Null
Copy-Item -Path (Join-Path $sdk "*") -Destination $tools -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination $staging
Copy-Item -LiteralPath (Join-Path $repoRoot "desktop\Install-REPS2.ps1") -Destination $staging

$prerequisites = Join-Path $staging "prerequisites"
New-Item -ItemType Directory -Path $prerequisites -Force | Out-Null
$vcRedist = Join-Path $prerequisites "vc_redist.x64.exe"
Invoke-WebRequest -Uri $VcRedistUrl -OutFile $vcRedist -UseBasicParsing
$signature = Get-AuthenticodeSignature -LiteralPath $vcRedist
if ($signature.Status -ne "Valid" -or
    $signature.SignerCertificate.Subject -notmatch "Microsoft Corporation") {
    throw "The Visual C++ redistributable failed Authenticode validation."
}

$zipName = "REPS2-Desktop-Alpha-windows-x64.zip"
$zipPath = Join-Path $output $zipName
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zipPath -CompressionLevel Optimal
$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$zipPath.sha256", "$zipHash  $zipName`n", [Text.UTF8Encoding]::new($false))
Write-Host "Portable package: $zipPath"

if (-not $SkipInstaller) {
    $makensis = if ($env:MAKENSIS) { $env:MAKENSIS } else { (Get-Command makensis -ErrorAction SilentlyContinue).Source }
    if (-not $makensis) {
        $candidate = Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"
        if (Test-Path -LiteralPath $candidate) { $makensis = $candidate }
    }
    if (-not $makensis) {
        throw "makensis was not found. Install NSIS or pass -SkipInstaller."
    }

    $env:REPS2_APP_VERSION = $appVersion
    $env:REPS2_APP_FILE_VERSION = $appFileVersion
    $env:REPS2_PUBLISH_DIR = $staging
    $env:REPS2_OUTPUT_DIR = $output
    & $makensis /V2 (Join-Path $repoRoot "desktop\installer\REPS2.nsi")
    if ($LASTEXITCODE -ne 0) {
        throw "makensis failed with exit code $LASTEXITCODE"
    }

    $setupPath = Join-Path $output "REPS2-Setup-windows-x64.exe"
    if (-not (Test-Path -LiteralPath $setupPath -PathType Leaf)) {
        throw "The setup executable was not produced."
    }
    $setupHash = (Get-FileHash -LiteralPath $setupPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$setupPath.sha256", "$setupHash  REPS2-Setup-windows-x64.exe`n", [Text.UTF8Encoding]::new($false))
    Write-Host "Setup executable: $setupPath"
}
