# ADR 0133: Six-layer product architecture and repository restructuring

- **Status:** Accepted.
- **Date:** 2026-11-13.
- **Scope:** Repository-wide structural ownership of the REPS2 desktop product.

## Context

REPS2 grew as a Rust toolchain plus a C++20 runtime and engine, with a WinUI
desktop shell built directly on code-behind that spawned `reps2.exe` and
`rg-reps2-engine.exe` inline. That shape had three problems:

- No stable contract boundary existed between the UI and the toolchain; the UI
  constructed CLI arguments and parsed run reports.
- Services and presentation were fused: page code-behind owned filtering,
  status assembly, probe execution and report parsing.
- The PC host side (graphics/audio/input/platform) had no owned home at all,
  so nothing prevented runtime code from reaching for Win32 directly.

## Decision

Adopt six architectural layers and move the code to match them:

| # | Layer | Location | Contents |
|---|-------|----------|----------|
| 01 | Frontend | `desktop/REPS2.Desktop` | WinUI 3 pages, `ViewModels/`, composition root (`DesktopAppContext`) |
| 02 | Application API | `desktop/REPS2.Desktop.Api` | `Contracts/` (versioned DTOs, service interfaces, `IToolchainClient`), `Ipc/` (`ProcessRunner`, `ProcessToolchainClient`) |
| 03 | Application services | `desktop/REPS2.Desktop.Core` | `LibraryService`, `BuildCoordinator`, `SessionService`, `SettingsStore`, `DiagnosticsService`, job/catalog/relink services |
| 04 | Backend toolchain | `rust/` | unchanged `reps2-frontend` |
| 05 | RG REPS2 ENGINE | `engine/` + `runtime/include` | unchanged; still never references `desktop/` |
| 06 | Graphics & host backend | `host/` (new CMake static library `REPS2::Host`) | `graphics.hpp` (API probe, software reference rasterizer), `input.hpp`, `audio.hpp`, `display.hpp`, `platform.hpp` (Win32/Null window) |

Specific decisions:

1. **Contracts are assemblies, not HTTP.** Layer 02 is a class library of
   records and interfaces. The only IPC is local process invocation with
   argument lists and exit/status results carrying `ContractVersions.Toolchain`;
   no REST/HTTP, no sockets, no microservices.
2. **The UI holds interfaces.** `DesktopAppContext` is typed against
   `IGameLibraryService`, `IBuildService`, `ISessionService`,
   `ISettingsService`, `IDiagnosticsService` and `IToolchainClient`. Pages own a
   view model each; code-behind only marshals UI events.
3. **The IPC client owns CLI syntax.** `ProcessToolchainClient` builds the
   `add-game` / `recompile-iso` / `dev` / runner argument lists. Services never
   assemble tool arguments.
4. **Probe parsing lives in the service layer.** `SessionService` validates the
   `rg.reps2.engine.run_report.v1` kind and extracts outcome/PC/instruction
   count; the UI only renders a `ProbeReport`.
5. **Host backends report what exists.** `probe_graphics_api` checks for
   `d3d12.dll`/`vulkan-1.dll`; `create_graphics_backend` returns `nullptr` for
   an unbound GPU API instead of fabricating a device. The software reference
   backend rasterizes deterministic RGBA frames for headless validation.
6. **No empty scaffolding.** Every layer contains working code used by an
   existing flow or test; the host library is exercised by `reps2_host_smoke`.

## Consequences

- `DesktopSettings`, `GameSummary`, `LibrarySnapshot`, `JobRecord`,
  `NativeBuildRecord`, `ToolchainResolution` and `ProbeReport` moved to
  `REPS2.Desktop.Api.Contracts`; `ProcessRunner` moved to
  `REPS2.Desktop.Api.Ipc`. Core and the tests reference the API assembly.
- `BuildCoordinator` now requires an `IToolchainClient` and no longer builds
  frontend arguments; the CMake/MSVC invocations remain in the service layer
  through the same IPC transport.
- The desktop project references both `REPS2.Desktop.Api` and
  `REPS2.Desktop.Core`; the solution and CI restore/build include the new
  project automatically.
- Root CMake gains `REPS2_BUILD_HOST` (default ON) and builds `host/`,
  registering `reps2_host_smoke` with CTest on all platforms.
- The UI never promotes compatibility state; probe/result handling still only
  reports evidence.

## Validation

- Desktop: `dotnet build REPS2.Desktop.csproj -c Release -r win-x64
  --self-contained true -warnaserror` succeeds; the 31 Core tests pass; the
  app was launched and the Library view-model bindings render (counts, notice,
  empty state) under the system theme.
- Native: CMake Release build includes `reps2_host`; `reps2_host_smoke` passes
  and `ctest -C Release` stays green.
- Rust: `cargo fmt/clippy/test --workspace` remain green; no frontend change.
