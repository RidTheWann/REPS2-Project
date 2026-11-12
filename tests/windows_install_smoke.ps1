# SPDX-License-Identifier: GPL-3.0-or-later
# Windows x64 install-tree smoke: installed tools, headers, static library,
# MSVC runtime, and generated native DLL load from a path with Unicode/spaces.

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

if ($env:PROCESSOR_ARCHITECTURE -ne "AMD64") {
    throw "The Windows install-tree gate must run on x64; detected $env:PROCESSOR_ARCHITECTURE"
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = Join-Path $repoRoot "build"
if (-not $env:RUNNER_TEMP) {
    throw "RUNNER_TEMP is required for the isolated install smoke test"
}

# The relocated checks must not be able to resolve a DLL from an install tree,
# so every portable run below uses a PATH holding only the extracted bundle and
# system directories. Keep the baseline for the CMake configure/build steps.
$baselinePath = $env:Path
$systemOnlyPath = (@(
    (Join-Path $env:SystemRoot "System32"),
    $env:SystemRoot,
    (Join-Path $env:SystemRoot "System32/WindowsPowerShell/v1.0")
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }) -join ';'

# Write text as UTF-8 without a BOM and with LF endings, so the bundle's JSON
# and checksum files are consumable by strict JSON readers and `sha256sum -c`.
function Write-Utf8LfFile {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Text
    )
    [System.IO.File]::WriteAllText(
        $Path,
        $Text.Replace("`r`n", "`n"),
        [System.Text.UTF8Encoding]::new($false)
    )
}

$installRoot = Join-Path $env:RUNNER_TEMP "RG REPS2 Engine-日本 install"
$consumerRoot = Join-Path $env:RUNNER_TEMP "RG REPS2 Consumer-日本"
$runRoot = Join-Path $env:RUNNER_TEMP "RG REPS2 Run-日本"
foreach ($path in @($installRoot, $consumerRoot, $runRoot)) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}

cmake --install $buildRoot --config Release --prefix $installRoot
if ($LASTEXITCODE -ne 0) {
    throw "CMake install failed with exit code $LASTEXITCODE"
}

$installedFrontend = Join-Path $installRoot "bin/reps2.exe"
$installedRunner = Join-Path $installRoot "bin/rg-reps2-engine.exe"
$installedLibrary = Join-Path $installRoot "lib/rg_reps2_engine.lib"
$installedSessionHeader = Join-Path $installRoot "include/rg_reps2_engine/session.hpp"
$installedLoaderHeader = Join-Path $installRoot "include/rg_reps2_engine/module_loader.hpp"
$installedRuntimeHeader = Join-Path $installRoot "include/reps2/runtime/ee_state.hpp"
foreach ($required in @(
    $installedFrontend, $installedRunner, $installedLibrary,
    $installedSessionHeader, $installedLoaderHeader, $installedRuntimeHeader
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing installed Windows deliverable: $required"
    }
}

$systemRuntime = @(Get-ChildItem -LiteralPath (Join-Path $installRoot "bin") -Filter "vcruntime*.dll" -File)
$cppRuntime = @(Get-ChildItem -LiteralPath (Join-Path $installRoot "bin") -Filter "msvcp*.dll" -File)
if ($systemRuntime.Count -eq 0 -or $cppRuntime.Count -eq 0) {
    throw "The install tree is missing the MSVC C/C++ runtime redistributable DLLs"
}

$moduleSource = Get-ChildItem -LiteralPath $buildRoot -Filter "reps2_native_module.dll" -File -Recurse |
    Where-Object { $_.FullName -like "*native-build-driver*" } |
    Select-Object -First 1
if ($null -eq $moduleSource) {
    throw "The Windows native-build driver did not leave its generated DLL for install-tree verification"
}

$pluginDirectory = Join-Path $installRoot "plugins_日本 語"
New-Item -ItemType Directory -Path $pluginDirectory -Force | Out-Null
$modulePath = Join-Path $pluginDirectory "RG-REPS2-日本-Module.dll"
Copy-Item -LiteralPath $moduleSource.FullName -Destination $modulePath

Push-Location $runRoot
try {
    $frontendVersion = & $installedFrontend version
    if ($LASTEXITCODE -ne 0 -or -not ($frontendVersion -match "reps2")) {
        throw "Installed Rust frontend did not run from the isolated install tree"
    }

    $runnerVersion = & $installedRunner --version
    if ($LASTEXITCODE -ne 0 -or -not ($runnerVersion -match "RG REPS2 ENGINE v")) {
        throw "Installed RG REPS2 ENGINE runner did not run from the isolated install tree"
    }

    $runnerHelp = & $installedRunner --help
    if ($LASTEXITCODE -ne 0 -or -not ($runnerHelp -match "Usage:")) {
        throw "Installed runner help did not execute from outside the build tree"
    }

    $reportLines = & $installedRunner $modulePath --budget 64 --max-batches 1 --instruction-trace
    $runnerStatus = $LASTEXITCODE
    if ($runnerStatus -ne 0) {
        throw "Installed runner failed to load/execute the generated DLL (exit $runnerStatus): $reportLines"
    }
    $report = ($reportLines -join "`n") | ConvertFrom-Json
    if ($report.kind -ne "rg.reps2.engine.run_report.v1") {
        throw "Installed runner returned an unexpected report kind"
    }
    if ($report.engine.name -ne "RG REPS2 ENGINE" -or $report.module.name -eq "") {
        throw "Installed runner returned missing engine/module identity"
    }
    $expectedModulePath = $modulePath.Replace('\', '/')
    if ($report.module.path -ne $expectedModulePath) {
        throw "UTF-8 path round-trip failed. Expected '$expectedModulePath', got '$($report.module.path)'"
    }
    if ([long]$report.session.instructions_executed -le 0) {
        throw "Installed runner did not execute generated guest instructions"
    }
    if ($report.runs.Count -ne 1 -or -not $report.runs[0].monotonic_counter) {
        throw "Installed runner did not return valid bounded-batch accounting"
    }
}
finally {
    Pop-Location
}

# Compile an independent consumer against only the installed headers and
# static library. Calling the out-of-line loader function proves the archive
# is linkable and its installed public contract is usable.
$consumerSource = @'
#include "rg_reps2_engine/engine.hpp"
#include "rg_reps2_engine/module_loader.hpp"

#include <filesystem>

int main() {
    using namespace rg::reps2::engine;
    const Engine engine{};
    if (engine.host_target().operating_system != OperatingSystem::windows
        || engine.host_target().architecture != Architecture::x86_64) {
        return 1;
    }
    PreparedNativeModule prepared{};
    const auto result = NativeModuleLoader::load(
        engine, std::filesystem::path{L"missing-rg-reps2-install-smoke.dll"}, prepared);
    return result.error == ModuleLoadError::path_resolution_failed ? 0 : 2;
}
'@
$consumerCmake = @'
cmake_minimum_required(VERSION 3.25)
project(rg_reps2_installed_consumer LANGUAGES CXX)
add_executable(rg_reps2_installed_consumer main.cpp)
target_include_directories(rg_reps2_installed_consumer PRIVATE "@INCLUDE_DIR@")
target_link_libraries(rg_reps2_installed_consumer PRIVATE "@ENGINE_LIBRARY@")
target_compile_features(rg_reps2_installed_consumer PRIVATE cxx_std_20)
'@
$consumerSourcePath = Join-Path $consumerRoot "main.cpp"
$consumerCmakePath = Join-Path $consumerRoot "CMakeLists.txt"
Set-Content -LiteralPath $consumerSourcePath -Value $consumerSource -Encoding utf8
$consumerCmake = $consumerCmake.Replace("@INCLUDE_DIR@", (Join-Path $installRoot "include").Replace('\', '/'))
$consumerCmake = $consumerCmake.Replace("@ENGINE_LIBRARY@", $installedLibrary.Replace('\', '/'))
Set-Content -LiteralPath $consumerCmakePath -Value $consumerCmake -Encoding utf8

$consumerBuild = Join-Path $consumerRoot "build"
cmake -S $consumerRoot -B $consumerBuild -A x64
if ($LASTEXITCODE -ne 0) {
    throw "Independent installed-consumer CMake configure failed"
}
cmake --build $consumerBuild --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Independent installed-consumer build failed"
}
$consumerExe = Join-Path $consumerBuild "Release/rg_reps2_installed_consumer.exe"
$installBin = Join-Path $installRoot "bin"
$env:Path = "$installBin;$baselinePath"
Push-Location $runRoot
try {
    & $consumerExe
    if ($LASTEXITCODE -ne 0) {
        throw "Installed public C++ API consumer failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
    $env:Path = $baselinePath
}

# Produce a relocatable developer SDK bundle. The synthetic native DLL is
# intentionally labeled as a test fixture, never as a playable game module.
if ($runnerVersion -match '^RG REPS2 ENGINE v(?<version>[0-9]+\.[0-9]+\.[0-9]+) \(ABI (?<abi>[0-9]+)\)$') {
    $engineVersion = $Matches.version
    $engineAbi = [int]$Matches.abi
}
else {
    throw "Cannot parse stable RG REPS2 ENGINE version/ABI from: $runnerVersion"
}
$sourceCommit = if ($env:SOURCE_COMMIT) {
    $env:SOURCE_COMMIT
}
elseif ($env:GITHUB_SHA) {
    $env:GITHUB_SHA
}
else {
    "local"
}
$runtimeAbiHeader = Get-Content -LiteralPath (
    Join-Path $installRoot "include/reps2/runtime/abi_version.hpp") -Raw
if ($runtimeAbiHeader -notmatch 'REPS2_RUNTIME_ABI_VERSION\s+([0-9]+)') {
    throw "Cannot read REPS2_RUNTIME_ABI_VERSION from the installed headers"
}
$runtimeAbi = [int]$Matches[1]
$bundleManifest = [ordered]@{
    kind = "rg.reps2.engine.windows_bundle.v1"
    schema_version = 1
    engine = [ordered]@{
        name = "RG REPS2 ENGINE"
        version = $engineVersion
        engine_abi_version = $engineAbi
        runtime_abi_version = $runtimeAbi
    }
    target = [ordered]@{
        operating_system = "windows"
        architecture = "x86_64"
        toolchain = "MSVC"
    }
    package_type = "developer_sdk"
    source_commit = $sourceCommit
    contents = @(
        "bin/reps2.exe",
        "bin/rg-reps2-engine.exe",
        "bin/*.dll",
        "lib/rg_reps2_engine.lib",
        "include/reps2/",
        "include/rg_reps2_engine/",
        "LICENSE",
        "plugins_日本 語/RG-REPS2-日本-Module.dll",
        "bundle.json",
        "checksums.sha256"
    )
    sample_module = [ordered]@{
        name = "synthetic-generated-cfg-fixture"
        playable = $false
        warning = "CI-only synthetic AOT module; not a PS2 title or a bootable game."
    }
    limitations = @(
        "Trusted in-process native modules only; no sandbox.",
        "This is a developer SDK bundle, not a consumer launcher or signed installer.",
        "No retail PS2 title boot or gameplay compatibility is claimed."
    )
}
$bundleManifestPath = Join-Path $installRoot "bundle.json"
$manifestJson = $bundleManifest | ConvertTo-Json -Depth 8
Write-Utf8LfFile -Path $bundleManifestPath -Text ($manifestJson + "`n")

$checksumLines = @()
$bundleFiles = @(Get-ChildItem -LiteralPath $installRoot -File -Recurse | Sort-Object FullName)
foreach ($file in $bundleFiles) {
    $relative = $file.FullName.Substring($installRoot.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
    $digest = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumLines += "$digest *$relative"
}
Write-Utf8LfFile -Path (Join-Path $installRoot "checksums.sha256") `
    -Text (($checksumLines -join "`n") + "`n")

$bundlePath = Join-Path $env:RUNNER_TEMP "rg-reps2-engine-windows-x64-msvc.zip"
if (Test-Path -LiteralPath $bundlePath) {
    Remove-Item -LiteralPath $bundlePath -Force
}
Compress-Archive -Path (Join-Path $installRoot "*") -DestinationPath $bundlePath -CompressionLevel Optimal
if (-not (Test-Path -LiteralPath $bundlePath -PathType Leaf)) {
    throw "Portable Windows SDK ZIP was not created"
}
$bundleHash = (Get-FileHash -LiteralPath $bundlePath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Utf8LfFile -Path "$bundlePath.sha256" `
    -Text ("$bundleHash  $(Split-Path $bundlePath -Leaf)`n")

# Extract into a second fresh Unicode path, verify every payload digest, then
# run the host and compile a consumer against only the extracted SDK.
$portableRoot = Join-Path $env:RUNNER_TEMP "RG REPS2 Portable-日本 解凍"
if (Test-Path -LiteralPath $portableRoot) {
    Remove-Item -LiteralPath $portableRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $portableRoot -Force | Out-Null
Expand-Archive -LiteralPath $bundlePath -DestinationPath $portableRoot -Force

$portableManifestPath = Join-Path $portableRoot "bundle.json"
$portableManifest = Get-Content -LiteralPath $portableManifestPath -Raw -Encoding utf8 | ConvertFrom-Json
$portableManifestValid = ($portableManifest.kind -eq "rg.reps2.engine.windows_bundle.v1") -and ($portableManifest.engine.name -eq "RG REPS2 ENGINE") -and ($portableManifest.target.operating_system -eq "windows") -and ($portableManifest.target.architecture -eq "x86_64") -and ($portableManifest.sample_module.playable -eq $false) -and ($portableManifest.source_commit -eq $sourceCommit) -and ([int]$portableManifest.engine.engine_abi_version -eq $engineAbi) -and ([int]$portableManifest.engine.runtime_abi_version -eq $runtimeAbi)
if (-not $portableManifestValid) {
    throw "Extracted SDK bundle manifest failed schema/content validation"
}

$checksumPath = Join-Path $portableRoot "checksums.sha256"
$listedPayloads = @{}
foreach ($line in Get-Content -LiteralPath $checksumPath -Encoding utf8) {
    if ($line -notmatch '^([0-9a-f]{64}) \*(.+)$') {
        throw "Invalid SHA-256 entry in bundle: $line"
    }
    $expectedHash = $Matches[1]
    $relativeName = $Matches[2].Replace('\', '/')
    if ($relativeName -match '^(?:/|[A-Za-z]:)') {
        throw "Bundle entry is an absolute path: $relativeName"
    }
    if ($relativeName -match '(?:^|/)\.\.(?:/|$)') {
        throw "Bundle entry escapes the extraction root: $relativeName"
    }
    $relativePath = $relativeName.Replace('/', '\')
    $payloadPath = Join-Path $portableRoot $relativePath
    $resolvedPayload = [System.IO.Path]::GetFullPath($payloadPath)
    $resolvedRoot = [System.IO.Path]::GetFullPath($portableRoot)
    if (-not $resolvedPayload.StartsWith(
            $resolvedRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Bundle entry resolves outside the extraction root: $relativeName"
    }
    if (-not (Test-Path -LiteralPath $payloadPath -PathType Leaf)) {
        throw "Bundle is missing checksummed payload: $relativeName"
    }
    $actualHash = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "Bundle checksum mismatch: $relativeName"
    }
    $listedPayloads[$relativeName] = $true
}

# The manifest is the inventory: refuse an empty list and any extracted file it
# does not cover. `checksums.sha256` cannot hash itself, so it is the one
# allowed exception.
if ($listedPayloads.Count -lt 10) {
    throw "Bundle checksum manifest lists too few payloads: $($listedPayloads.Count)"
}
$listedPayloads["checksums.sha256"] = $true
foreach ($file in Get-ChildItem -LiteralPath $portableRoot -File -Recurse) {
    $relativeName = $file.FullName.Substring($portableRoot.Length).TrimStart(
        [char[]]@('\', '/')).Replace('\', '/')
    if (-not $listedPayloads.ContainsKey($relativeName)) {
        throw "Bundle contains a payload that is not in its checksum manifest: $relativeName"
    }
}

$portableRunner = Join-Path $portableRoot "bin/rg-reps2-engine.exe"
$portableModule = Join-Path $portableRoot "plugins_日本 語/RG-REPS2-日本-Module.dll"
$portableRunRoot = Join-Path $env:RUNNER_TEMP "RG REPS2 Portable Run-日本"
New-Item -ItemType Directory -Path $portableRunRoot -Force | Out-Null
$portableBin = Join-Path $portableRoot "bin"
# Scrub the install tree from PATH: the bundle must satisfy its own DLL imports.
$env:Path = "$portableBin;$systemOnlyPath"
Push-Location $portableRunRoot
try {
    $portableVersion = & $portableRunner --version
    if ($LASTEXITCODE -ne 0 -or $portableVersion -ne $runnerVersion) {
        throw "Relocated portable runner version/ABI check failed"
    }
    $portableOutput = & $portableRunner $portableModule --budget 64 --max-batches 1 --instruction-trace
    if ($LASTEXITCODE -ne 0) {
        throw "Relocated portable runner failed to load the generated DLL: $portableOutput"
    }
    $portableReport = ($portableOutput -join [Environment]::NewLine) | ConvertFrom-Json
    $portableExpectedModulePath = $portableModule.Replace('\', '/')
    $portableRunValid = ($portableReport.kind -eq "rg.reps2.engine.run_report.v1") -and ($portableReport.module.path -eq $portableExpectedModulePath) -and ([long]$portableReport.session.instructions_executed -gt 0)
    if (-not $portableRunValid) {
        throw "Relocated portable runner did not produce a valid execution report"
    }
}
finally {
    Pop-Location
    $env:Path = $baselinePath
}

$portableConsumerRoot = Join-Path $env:RUNNER_TEMP "RG REPS2 Portable Consumer-日本"
New-Item -ItemType Directory -Path $portableConsumerRoot -Force | Out-Null
Copy-Item -LiteralPath $consumerSourcePath -Destination (Join-Path $portableConsumerRoot "main.cpp")
$portableConsumerCmake = $consumerCmake
$installInclude = (Join-Path $installRoot "include").Replace('\', '/')
$portableInclude = (Join-Path $portableRoot "include").Replace('\', '/')
$installLibrary = $installedLibrary.Replace('\', '/')
$portableLibrary = (Join-Path $portableRoot "lib/rg_reps2_engine.lib").Replace('\', '/')
$portableConsumerCmake = $portableConsumerCmake.Replace($installInclude, $portableInclude)
if ($portableConsumerCmake -eq $consumerCmake) {
    throw "Portable consumer configure still references the install-tree include path"
}
$portableConsumerCmake = $portableConsumerCmake.Replace($installLibrary, $portableLibrary)
if ($portableConsumerCmake.Contains($installLibrary)) {
    throw "Portable consumer configure still references the install-tree library path"
}
if (-not $portableConsumerCmake.Contains($portableInclude)) {
    throw "Portable consumer configure does not reference the extracted include path"
}
Set-Content -LiteralPath (Join-Path $portableConsumerRoot "CMakeLists.txt") -Value $portableConsumerCmake -Encoding utf8
$portableConsumerBuild = Join-Path $portableConsumerRoot "build"
cmake -S $portableConsumerRoot -B $portableConsumerBuild -A x64
if ($LASTEXITCODE -ne 0) {
    throw "Portable SDK consumer configure failed"
}
cmake --build $portableConsumerBuild --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Portable SDK consumer build failed"
}
$portableConsumerExe = Join-Path $portableConsumerBuild "Release/rg_reps2_installed_consumer.exe"
# The extracted consumer runs against the bundle's headers and library too, so
# keep the install tree off PATH here as well.
$env:Path = "$portableBin;$systemOnlyPath"
Push-Location $portableRunRoot
try {
    & $portableConsumerExe
    if ($LASTEXITCODE -ne 0) {
        throw "Portable SDK public C++ consumer failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
    $env:Path = $baselinePath
}

Write-Host "Windows x64 install and portable SDK bundle smoke passed"
Write-Host "  Install root: $installRoot"
Write-Host "  Extracted SDK: $portableRoot"
Write-Host "  Bundle: $bundlePath"
Write-Host "  Bundle SHA-256: $bundleHash"
Write-Host "  Generated DLL: $modulePath"
Write-Host "  Guest instructions: $($report.session.instructions_executed)"
Write-Host "  Relocated guest instructions: $($portableReport.session.instructions_executed)"
