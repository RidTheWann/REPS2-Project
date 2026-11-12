# REPS2 Desktop Alpha — Windows x64

REPS2 Desktop Alpha is the first native Windows application shell for REPS2. It is a real WinUI 3 application, not the browser-based product prototype. The application integrates the existing Rust frontend and RG REPS2 ENGINE as child processes through the repository's CLI and versioned artifacts. It does not link the compiler or engine into the UI process.

## Included

- WinUI 3 / .NET 10 Studio Dark desktop shell.
- Library grid, title detail, text search, evidence-state filters (needs review, ready to build, native-built, playable), alphabetical sorting, result counts and clear-filter empty state; identity and compatibility evidence.
- Native Windows ISO picker and one-ISO import through \`reps2 add-game\`.
- Evidence-derived Imported, Analyzed, Translation incomplete, Translated, Native-built and Recovery-required states.
- Safe source-ISO recovery from Game Detail: a moved image is relinked only after exact content-addressed SHA-256 and recorded-size verification; manifest replacement is atomic.
- Native build orchestration through \`recompile-iso\`, CMake and MSVC Release, followed by build identity and ISO/ELF provenance checks.
- Persisted Build Queue with live stage/status updates, job messages, exit codes and safe output/log directory actions; shell-level build cancellation with process-tree termination; Developer Center reports, bounded diagnostic probes and local toolchain settings.
- Startup update checks, SHA-256-verified update downloads, a separate self-contained update helper, startup handshake and automatic rollback.
- Core unit tests for state derivation, library filtering/sorting, digest-verified ISO relink, child-process cancellation, semantic versions and safe update-package extraction.

## Honest Alpha boundaries

- **There is no Play button.** No title is currently promoted to Boot-tested or Playable by the desktop shell.
- A generated DLL, successful host build, or bounded runner exit does not prove the game booted.
- This Alpha does not contain a complete IOP interpreter, GS renderer, SPU2, controller gameplay, save/memory-card system or general-purpose PS2 compatibility promise.
- The library currently discovers entries from \`Library/games/*/game.json\`. Delete, cache reuse, parallel/cancellable job management, cover metadata and MSIX signing are not shipped.
- Probe reports are retained for inspection but do not automatically promote compatibility.
- Game cover art is not redistributed; the library uses a neutral PS2 placeholder.

## Build requirements

- Windows x64 (Windows 10 build 19041 APIs; Windows 10 22H2 / Windows 11 recommended).
- .NET 10 SDK and Windows App SDK 2.6.5.
- For game-native builds: CMake and Visual Studio Build Tools with the Desktop development with C++ workload and Windows SDK.
- The portable REPS2 Windows SDK bundle from the verified Windows CI install-tree job.

The desktop app and updater helper are published self-contained. Because this is an unpackaged WinUI 3 application, the Windows x64 Microsoft Visual C++ v14 Redistributable is also included and signature-checked during CI. Setup checks for the runtime and installs it only when missing; Windows may show a UAC prompt for that shared prerequisite. The C++ compiler toolchain is not bundled in the application; it is required only when building native title modules.

## Build and test

From the repository root in PowerShell:

\`\`\`powershell
dotnet restore .\desktop\REPS2.Desktop.sln -r win-x64
dotnet test .\desktop\tests\REPS2.Desktop.Core.Tests\REPS2.Desktop.Core.Tests.csproj -c Release --no-restore -r win-x64
dotnet publish .\desktop\REPS2.Desktop\REPS2.Desktop.csproj -c Release -r win-x64 --self-contained true -p:WindowsAppSDKSelfContained=true
dotnet publish .\desktop\REPS2.Desktop.Updater\REPS2.Desktop.Updater.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
\`\`\`

To assemble the same package locally after publishing and building the SDK install tree, run:

```powershell
.\desktop\package-windows.ps1 -PublishDir <desktop-publish-dir> -SdkDir <cmake-install-prefix> -OutputDir <output-dir>
```

The script validates the publish/install inputs, downloads the current Microsoft Visual C++ x64 Redistributable from Microsoft's official endpoint, verifies its Authenticode signature, stages the package, writes the portable ZIP and SHA-256 sidecar, and (when NSIS is installed) compiles the setup executable and its sidecar. CI performs the same steps and then silently installs and uninstalls the setup.

The CI workflow builds and verifies the REPS2 Windows SDK, tests the core services, publishes the self-contained WinUI app and updater helper, downloads the current Microsoft Visual C++ x64 Redistributable from Microsoft's official endpoint and validates its Authenticode signature, then creates both a Windows setup executable and a portable ZIP. CI silently installs and uninstalls the setup executable in a clean runner profile, verifies its installed files and Start Menu entry, and checks SHA-256 for both release packages before publishing them.

## First-time installation

**Recommended: use the Windows setup executable.**

1. Open the [GitHub Releases page](https://github.com/RidTheWann/REPS2-Project/releases) and download \`REPS2-Setup-windows-x64.exe\` from the latest Desktop Alpha prerelease.
2. Run the setup wizard. REPS2 installs for the current Windows user under `%LOCALAPPDATA%\Programs\REPS2`. If the Microsoft Visual C++ x64 runtime is missing, Windows requests administrator approval to install that shared prerequisite; otherwise setup remains per-user.
3. The setup registers REPS2 in Windows Apps / Installed apps, creates a Start Menu shortcut, and offers an optional desktop shortcut.
4. On the final page, select **Launch REPS2 Desktop** to start the application.
5. Select **Add Game** and choose one lawfully owned raw 2048-byte-sector PS2 ISO. REPS2 validates the ISO and boot ELF; the source ISO stays at its original path.
6. Open the title to inspect its evidence. **Build native module** becomes available once the title is Translated and the native toolchain is configured.

The NSIS setup wizard declares DPI awareness so its text is not bitmap-scaled by Windows on high-DPI displays. The wizard layout and component-page bitmap should still be reviewed at 100%, 125%, 150% and 200% scaling. The setup is a per-user installer and deliberately uses the same writable installation location expected by the Alpha updater. The Visual C++ prerequisite is installed machine-wide only when missing. The application data, library, reports and game ISOs remain outside the installation directory and are preserved when the application is uninstalled.

**Portable / developer option:** download \`REPS2-Desktop-Alpha-windows-x64.zip\`, extract it, and use \`Install-REPS2.ps1\` only when a script-driven manual installation is preferred. The setup executable is the normal path for end users.

The default library is \`%LOCALAPPDATA%\\REPS2\\Library\`. Build outputs, jobs, reports, update downloads and settings live under \`%LOCALAPPDATA%\\REPS2\\Desktop\`; diagnostics and the update log live under \`%LOCALAPPDATA%\\REPS2\\Logs\`. These user-data folders are outside the application directory and are preserved across updates.

## One-click Alpha updates

- REPS2 checks the public GitHub Releases feed at startup when **Automatically check for REPS2 Desktop Alpha updates on startup** is enabled (default).
- When a newer prerelease tagged \`desktop-v<semver>\` is available, the header button changes to **Update <version>**. Click it once to download, verify and apply the update.
- The app downloads the release ZIP and its SHA-256 sidecar. It rejects packages from other repositories or unexpected asset names.
- A separate helper is copied to \`%LOCALAPPDATA%\\REPS2\\Updater\`, outside the folder being replaced. It waits for the UI to exit, verifies the package checksum again, safely extracts to a fresh staging directory, swaps the app folder, restarts REPS2 and waits for a startup signal.
- If the updated app fails to complete startup, the helper restores the previous application folder and attempts to relaunch it.
- CI publishes the Windows setup executable and portable ZIP, each with its own SHA-256 sidecar, as a GitHub prerelease when a \`desktop-v*\` tag is pushed. The ZIP remains the in-app update payload; the setup executable is for first-time installation and repair. Untagged CI artifacts are not offered as updates.

The SHA-256 sidecars detect corruption or mismatched downloads; they are not digital publisher signatures. The included Visual C++ runtime prerequisite is independently Authenticode-verified against Microsoft's signing certificate during CI. The setup executable and application are not Authenticode-signed in this Alpha, so Windows may show an unverified-publisher or SmartScreen warning. Authenticode signing remains a release-hardening task.

## Security notes

Generated native modules execute with the runner process's privileges and are not sandboxed. The desktop UI warns before starting a bounded probe. Update extraction rejects path traversal, duplicate paths, symbolic links and oversized entries. Process arguments are passed separately rather than interpolated into a shell command.
