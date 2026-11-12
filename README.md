<div align="center">

# REPS2

**An independent PlayStation 2 static AOT recompilation toolchain for native Windows x64**

<p>
  <a href="https://github.com/RidTheWann/REPS2-Project/actions/workflows/ci.yml">
    <img alt="CI status on GitHub" src="https://img.shields.io/github/actions/workflow/status/RidTheWann/REPS2-Project/ci.yml?branch=main&amp;label=CI&amp;logo=github&amp;logoColor=white&amp;style=flat">
  </a>
  <a href="https://www.rust-lang.org/">
    <img alt="Rust 1.88+" src="https://img.shields.io/badge/Rust-1.88%2B-D34516?logo=rust&amp;logoColor=DEA584&amp;style=flat">
  </a>
  <a href="https://isocpp.org/">
    <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&amp;logoColor=659AD2&amp;style=flat">
  </a>
  <a href="https://learn.microsoft.com/windows/apps/winui/winui3/">
    <img alt="WinUI 3" src="https://img.shields.io/badge/WinUI-3-0078D4?logo=data:image%2Fsvg%2Bxml%3Bbase64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzNjUiIGhlaWdodD0iMzMyIiB2aWV3Qm94PSIwIDAgMzY1IDMzMiI%2BCjxkZWZzPgo8bGluZWFyR3JhZGllbnQgaWQ9ImEiIHgxPSI5OS41IiB5MT0iOC40IiB4Mj0iMjguNCIgeTI9IjEyMS4yIj48c3RvcCBzdG9wLWNvbG9yPSIjM0JENUZGIi8%2BPHN0b3Agb2Zmc2V0PSIxIiBzdG9wLWNvbG9yPSIjMEZBRkZGIi8%2BPC9saW5lYXJHcmFkaWVudD4KPGxpbmVhckdyYWRpZW50IGlkPSJiIiB4MT0iOTkuNSIgeTE9IjE2NiIgeDI9Ijg2LjgiIHkyPSIzMzMuNiI%2BPHN0b3Agc3RvcC1jb2xvcj0iIzBGQUZGRiIvPjxzdG9wIG9mZnNldD0iMSIgc3RvcC1jb2xvcj0iIzAwNzhENCIvPjwvbGluZWFyR3JhZGllbnQ%2BCjxsaW5lYXJHcmFkaWVudCBpZD0iYyIgeDE9IjE4Mi41IiB5MT0iNi44IiB4Mj0iNDU3LjMiIHkyPSIxNjQuMiI%2BPHN0b3Agc3RvcC1jb2xvcj0iIzBGQUZGRiIvPjxzdG9wIG9mZnNldD0iMSIgc3RvcC1jb2xvcj0iIzAwNzhENCIvPjwvbGluZWFyR3JhZGllbnQ%2BCjxsaW5lYXJHcmFkaWVudCBpZD0iZCIgeDE9IjExNi45IiB5MT0iNDkuOSIgeDI9IjE0MS45IiB5Mj0iMTYwLjYiPjxzdG9wIHN0b3AtY29sb3I9IiNFNkY5RkUiLz48c3RvcCBvZmZzZXQ9IjEiIHN0b3AtY29sb3I9IiM5REVBRkYiLz48L2xpbmVhckdyYWRpZW50Pgo8bGluZWFyR3JhZGllbnQgaWQ9ImUiIHgxPSI1MS4yIiB5MT0iMTgwLjUiIHgyPSIxNDkuNyIgeTI9IjMwNy43Ij48c3RvcCBzdG9wLWNvbG9yPSIjRTZGOUZFIi8%2BPHN0b3Agb2Zmc2V0PSIxIiBzdG9wLWNvbG9yPSIjOURFQUZGIi8%2BPC9saW5lYXJHcmFkaWVudD4KPGxpbmVhckdyYWRpZW50IGlkPSJmIiB4MT0iMjE1LjIiIHkxPSI0OS45IiB4Mj0iMzgyLjQiIHkyPSIxMjAuOSI%2BPHN0b3Agc3RvcC1jb2xvcj0iIzlERUFGRiIvPjxzdG9wIG9mZnNldD0iMSIgc3RvcC1jb2xvcj0iIzNCRDVGRiIvPjwvbGluZWFyR3JhZGllbnQ%2BCjwvZGVmcz4KPHBhdGggZD0iTTI0OC43NDEgLjEwM0gxMTYuMjZjLTE3Ljc4MiAwLTM0LjIxMyA5LjQ4Ny00My4xMDUgMjQuODg2TDcuMTU3IDEzOS4zMDJDMi40NjggMTQ3LjQyNCAwIDE1Ni42MzUgMCAxNjYuMDEyaDE4Mi41YzAtMS4wNTIuMjc3LTEuODg1LjgwMy0yLjc5NkwyNzMuNjI3IDYuNzcxQzI2Ni4xODQgMi40NzUgMjU3LjYzMi4xMDMgMjQ4Ljc0MS4xMDNaIiBmaWxsPSJ1cmwoI2EpIi8%2BCjxwYXRoIGQ9Ik0yNDguNzQxIDMzMS45MjFIMTE2LjI2Yy0xNy43ODIgMC0zNC4yMTQtOS40ODctNDMuMTA1LTI0Ljg4N0w3LjE1NyAxOTIuNzIxQzIuNTAyIDE4NC42NTguMDM1IDE3NS4zMTkgMCAxNjYuMDEyaDE4Mi41YzAgLjc5LjIwNyAxLjc2NS42MDIgMi40NDlsOTAuNTI0IDE1Ni43OTJjLTcuNDQxIDQuMjk3LTE1Ljk5NCA2LjY2OC0yNC44ODUgNi42NjhaIiBmaWxsPSJ1cmwoI2IpIi8%2BCjxwYXRoIGQ9Im0zNTcuODQzIDE5Mi43MTktNjUuOTk5IDExNC4zMTRjLTQuNDQ1IDcuNjk5LTEwLjc3NiAxMy45MjEtMTguMjE4IDE4LjIxOEwxODMuMTAyIDE2OC40NTljLS4zOTUtLjY4NC0uNjAyLTEuNDU5LS42MDItMi4yNDggMC0xLjA1Mi4yNzctMi4wODYuODAzLTIuOTk3TDI3My42MjcgNi43N2M3LjQ0MSA0LjI5NyAxMy43NzIgMTAuNTE4IDE4LjIxNyAxOC4yMThsNjUuOTk5IDExNC4zMTNjNC42ODggOC4xMjEgNy4xNTcgMTcuMzMzIDcuMTU3IDI2LjcxIDAgOS4zNzctMi40NjkgMTguNTktNy4xNTcgMjYuNzA5WiIgZmlsbD0idXJsKCNjKSIvPgo8cGF0aCBkPSJtMTgyLjYxOCA0OS44NzYtMzMuNTI1IDU4LjA2OC0zMi40NTMgNTYuMjQ1Yy0uMzIxLjU1NC0uNDg5IDEuMTk2LS40ODkgMS44MzZsLjAwNi4xODhINTEuMjM1bC0uMDAyLS4yMDFjMC0yLjEyNC41NDYtNC4yNDcgMS42NC02LjE2IDUuNjkzLTkuOTYgMTcuNjE4LTMwLjY0MSAyOS44OTctNTEuOTA4aC0uMDRsMjguNzM2LTQ5Ljc3M2MyLjk2NC01LjEzMyA4LjQ0MS04LjI5NSAxNC4zNjgtOC4yOTVoNTYuNzg0WiIgZmlsbD0idXJsKCNkKSIvPgo8cGF0aCBkPSJtMTQ5LjAzIDIyNC4xOTYuMDI0LjAtMzIuNDE0LTU2LjM1OGEzLjY0IDMuNjQgMCAwIDEtLjQ4My0xLjYyM2wtLjAwNi0uMTg3SDUxLjIzM2MuMDAyIDIuMTE5LjU0OSA0LjIzNyAxLjY0IDYuMTQ1IDUuNzAzIDkuOTc4IDE3LjY2IDMwLjcxNSAyOS45NjEgNTIuMDE5aC0uMDRsMjguNjcyIDQ5LjY2YzIuOTYzIDUuMTMzIDguNDQgOC4yOTYgMTQuMzY4IDguMjk2aDU2Ljc5OWwtMzMuNjAzLTU3Ljk1MloiIGZpbGw9InVybCgjZSkiLz4KPHBhdGggZD0ibTIxNS4yMjcgMTA3Ljk0NCAzMy41ODggNTguMTg2LTMzLjUyMyA1OC4wNjMgMS43OTUgMy4xMjIgMzEuNjQ1IDU0LjgzMyA2MC44NzYtMTA1LjM5MyAyLjYyNC00LjU5NmMxLjA4OS0xLjkwOCAxLjYzMy00LjAyNyAxLjYzMy02LjE0NSAwLTIuMTE5LS41NDQtNC4yMzctMS42MzMtNi4xNDdsLTIuNjQ5LTQuNjM4LTI3LjI0MS00Ny4xNjhoLS4wMmwtMzMuNTktNTguMTg2LTMzLjUwNSA1OC4wNjlaIiBmaWxsPSJ1cmwoI2YpIi8%2BCjwvc3ZnPg==&amp;style=flat">
  </a>
  <a href="https://dotnet.microsoft.com/">
    <img alt=".NET 10" src="https://img.shields.io/badge/.NET-10-512BD4?logo=dotnet&amp;logoColor=512BD4&amp;style=flat">
  </a>
  <a href="https://cmake.org/">
    <img alt="CMake 3.25+" src="https://img.shields.io/badge/CMake-3.25%2B-064F8C?logo=data:image%2Fsvg%2Bxml%3Bbase64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxMjggMTI4Ij48cGF0aCBmaWxsPSIjMDY0RjhDIiBkPSJNNjIuOC40TC4zIDEyMy44bDY4LjEtNTcuOXoiLz48cGF0aCBmaWxsPSIjMjQ5ODQ3IiBkPSJNMTIzLjggMTI3LjdsLTg0LTMzLjlMMCAxMjcuN3oiLz48cGF0aCBmaWxsPSIjQkUyMTI4IiBkPSJNMTI4IDEyNi42TDY1LjYgMi41bDkuMiAxMDIuNnoiLz48cGF0aCBmaWxsPSIjQ0RDRENFIiBkPSJNNzEuOSAxMDRsLTMuMS0zNC45TDQyIDkyeiIvPjwvc3ZnPg%3D%3D&amp;style=flat">
  </a>
  <a href="LICENSE">
    <img alt="GPL-3.0-or-later license" src="https://img.shields.io/badge/License-GPL--3.0--or--later-A42E2B?logo=data:image%2Fsvg%2Bxml%3Bbase64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxNiIgaGVpZ2h0PSIxNiIgdmlld0JveD0iMCAwIDE2IDE2Ij48cGF0aCBmaWxsPSIjRkZGRkZGIiBkPSJNOC43NS43NVYyaC45ODVjLjMwNCAwIC42MDMuMDguODY3LjIzMWwxLjI5LjczNmMuMDM4LjAyMi4wOC4wMzMuMTI0LjAzM2gyLjIzNGEuNzUuNzUgMCAwIDEgMCAxLjVoLS40MjdsMi4xMTEgNC42OTJhLjc1Ljc1IDAgMCAxLS4xNTQuODM4bC0uNTMtLjUzLjUyOS41MzEtLjAwMS4wMDItLjAwMi4wMDItLjAwNi4wMDYtLjAwNi4wMDUtLjAxLjAxLS4wNDUuMDRjLS4yMS4xNzYtLjQ0MS4zMjctLjY4Ni40NUMxNC41NTYgMTAuNzggMTMuODggMTEgMTMgMTFhNC40OTggNC40OTggMCAwIDEtMi4wMjMtLjQ1NCAzLjU0NCAzLjU0NCAwIDAgMS0uNjg2LS40NWwtLjA0NS0uMDQtLjAxNi0uMDE1LS4wMDYtLjAwNi0uMDA0LS4wMDR2LS4wMDFhLjc1Ljc1IDAgMCAxLS4xNTQtLjgzOEwxMi4xNzggNC41aC0uMTYyYy0uMzA1IDAtLjYwNC0uMDc5LS44NjgtLjIzMWwtMS4yOS0uNzM2YS4yNDUuMjQ1IDAgMCAwLS4xMjQtLjAzM0g4Ljc1VjEzaDIuNWEuNzUuNzUgMCAwIDEgMCAxLjVoLTYuNWEuNzUuNzUgMCAwIDEgMC0xLjVoMi41VjMuNWgtLjk4NGEuMjQ1LjI0NSAwIDAgMC0uMTI0LjAzM2wtMS4yODkuNzM3Yy0uMjY1LjE1LS41NjQuMjMtLjg2OS4yM2gtLjE2MmwyLjExMiA0LjY5MmEuNzUuNzUgMCAwIDEtLjE1NC44MzhsLS41My0uNTMuNTI5LjUzMS0uMDAxLjAwMi0uMDAyLjAwMi0uMDA2LjAwNi0uMDE2LjAxNS0uMDQ1LjA0Yy0uMjEuMTc2LS40NDEuMzI3LS42ODYuNDVDNC41NTYgMTAuNzggMy44OCAxMSAzIDExYTQuNDk4IDQuNDk4IDAgMCAxLTIuMDIzLS40NTQgMy41NDQgMy41NDQgMCAwIDEtLjY4Ni0uNDVsLS4wNDUtLjA0LS4wMTYtLjAxNS0uMDA2LS4wMDYtLjAwNC0uMDA0di0uMDAxYS43NS43NSAwIDAgMS0uMTU0LS44MzhMMi4xNzggNC41SDEuNzVhLjc1Ljc1IDAgMCAxIDAtMS41aDIuMjM0YS4yNDkuMjQ5IDAgMCAwIC4xMjUtLjAzM2wxLjI4OC0uNzM3Yy4yNjUtLjE1LjU2NC0uMjMuODY5LS4yM2guOTg0Vi43NWEuNzUuNzUgMCAwIDEgMS41IDBabTIuOTQ1IDguNDc3Yy4yODUuMTM1LjcxOC4yNzMgMS4zMDUuMjczczEuMDItLjEzOCAxLjMwNS0uMjczTDEzIDYuMzI3Wm0tMTAgMGMuMjg1LjEzNS43MTguMjczIDEuMzA1LjI3M3MxLjAyLS4xMzggMS4zMDUtLjI3M0wzIDYuMzI3WiIvPjwvc3ZnPg==&amp;style=flat">
  </a>
</p>

[Roadmap dashboard](https://ridthewann.github.io/REPS2-Project/) ·
[Documentation](#documentation) ·
[Desktop Alpha](desktop/README.md)

</div>

---

## Status Project

<p align="center">
  <a href="https://ridthewann.github.io/REPS2-Project/">
    <img src="https://ridthewann.github.io/REPS2-Project/progress-grid.svg" alt="REPS2 project progress grid">
  </a>
</p>

The grid is grouped by milestone. Hover over a tile in the live dashboard for its full description. Progress is calculated only from checked items in [docs/ROADMAP.md](docs/ROADMAP.md); it is not a game-compatibility, boot-success, or release-readiness score. The dashboard is regenerated by GitHub Actions from the roadmap and [verified project state](docs/PROJECT_STATE.md).

> **Experimental AOT foundation (v0.7.0) · Windows Desktop Alpha**  
> REPS2 can import an owner-supplied PS2 ISO, inspect its boot ELF, analyze a documented R5900 subset, generate deterministic C++20, and build native modules for the project-owned RG REPS2 ENGINE. Windows x64 is the active desktop target; Linux remains a CI portability target.

**Compatibility boundary:** no PS2 title is currently verified as Boot-tested or Playable. Successful import, analysis, translation, native compilation, synthetic execution, or a bounded diagnostic run must not be interpreted as proof that a retail game boots.

## What REPS2 does

REPS2 is designed around a multi-game PS2 ISO-to-native pipeline:

1. **Import and inspect** an owned raw PS2 ISO and identify its boot ELF.
2. **Analyze** the ELF and discover code regions, control flow, and unsupported boundaries.
3. **Translate** supported R5900 instructions into a typed, verified intermediate representation.
4. **Generate** deterministic C++20 source and versioned build metadata.
5. **Build and execute** a native title module through the RG REPS2 ENGINE runtime boundary.

The original ISO remains an external user asset. Game images, BIOS dumps, proprietary SDKs, and copyrighted game content are not stored in this repository.

## Architecture

| Component | Responsibility | Technology |
|---|---|---|
| Frontend | ISO/ELF inspection, R5900 decoding, discovery, CFG, typed IR, verification, C++ generation, manifests, and CLI | Rust |
| Generated title module | Deterministic native code emitted from the supported guest-code subset | C++20 |
| Runtime | Versioned native-module ABI, EE session state, 32 MiB EE RAM, bounded execution, and structured run reports | C++20 |
| Windows desktop | ISO library, evidence-derived lifecycle, native-build orchestration, job history, developer diagnostics, and update flow | WinUI 3 / .NET 10 |
| Build and validation | Native builds, Rust checks, desktop tests, packaging, and roadmap dashboard deployment | CMake / MSVC / GitHub Actions |

The desktop application invokes the existing Rust frontend and RG REPS2 ENGINE through child processes and versioned artifacts. The UI does not contain a second compiler implementation or link the engine through FFI.

## Implemented foundation

The current codebase includes:

- Raw 2048-byte-sector ISO inspection, boot-ELF extraction, and ISO/ELF provenance checks.
- Defensive ELF metadata parsing and R5900 CPU identification.
- Decoder, analysis, typed IR, verifier, and deterministic C++20 AOT generation for the documented instruction subset.
- CFG-aware control flow, delay-slot handling, main-RAM operations, selected EE FPU/MMI coverage, and explicit exception behavior.
- A versioned native-module ABI and the RG REPS2 ENGINE session/runner boundary.
- Bounded execution with structured developer reports; a clean host exit is not treated as a guest boot result.
- A Windows Desktop Alpha for library management, ISO import, evidence-derived status, build orchestration, diagnostics, settings, and release-based one-click updates.

The authoritative feature inventory and known gaps live in [docs/CAPABILITIES.md](docs/CAPABILITIES.md), [docs/PROJECT_STATE.md](docs/PROJECT_STATE.md), and [docs/COMPATIBILITY_MATRIX.md](docs/COMPATIBILITY_MATRIX.md).

## Build and test

### Requirements

- Rust 1.88 or newer (stable; Rust edition 2024).
- CMake 3.25 or newer.
- A C++20 toolchain: Visual Studio 2022 17.4+, GCC 12+, or Clang 15+.
- Git.

### Build the repository

Run from the repository root:

~~~sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
~~~

The Rust frontend is produced at build/cargo-target/release/reps2.exe on Windows and build/cargo-target/release/reps2 on Linux.

For Rust-only validation:

~~~sh
cargo fmt --manifest-path rust/Cargo.toml --all -- --check
cargo clippy --manifest-path rust/Cargo.toml --workspace --all-targets -- -D warnings
cargo test --manifest-path rust/Cargo.toml --workspace --locked
~~~

### CLI entry points

~~~text
reps2 version
reps2 disc-info <owned-disc.iso>
reps2 add-game <owned-disc.iso> [--library <directory>]
reps2 extract-boot-elf <owned-disc.iso> <output.elf>
reps2 elf-info <boot.elf>
reps2 analyze-elf <boot.elf>
reps2 recompile-iso <owned-disc.iso> <output-directory>
reps2 dev capabilities
reps2 dev sessions
reps2 dev inspect latest
~~~

See the [CLI guide](docs/CLI.md) for complete arguments, output layouts, limits, and report semantics.

## Windows Desktop Alpha

The native WinUI 3 application is the Windows-facing development shell for REPS2. It provides a game library, single-ISO import, title details, build queue, Developer Center, local settings, and a compatibility lifecycle derived from verified artifacts.

- [Download Windows Desktop Alpha installer](https://github.com/RidTheWann/REPS2-Project/releases) · [Portable ZIP and checksums](https://github.com/RidTheWann/REPS2-Project/releases)
- [Desktop Alpha guide](desktop/README.md)
- [Implementation scope and boundaries](docs/desktop-alpha-implementation.md)

The application currently has no Play button and does not claim that a title is playable. Native title builds require CMake and the MSVC C++ workload; those compiler tools are not bundled into the desktop package. The in-app updater checks published desktop-v* releases and verifies the package checksum before applying an update.

## Documentation

| Document | Purpose |
|---|---|
| [Product vision](docs/PRODUCT_VISION.md) | Product goals, constraints, and target workflow |
| [Architecture](docs/ARCHITECTURE.md) | Frontend, generated module, runtime, and subsystem boundaries |
| [Capabilities](docs/CAPABILITIES.md) | Implemented instruction, analysis, runtime, and CLI coverage |
| [Roadmap](docs/ROADMAP.md) | Milestones and implementation checklist |
| [Project state](docs/PROJECT_STATE.md) | Current evidence, real-title observations, and active boundaries |
| [Compatibility matrix](docs/COMPATIBILITY_MATRIX.md) | Per-title evidence and compatibility status |
| [CLI reference](docs/CLI.md) | Commands, arguments, artifacts, and developer workflows |
| [Release gates](docs/RELEASE_GATES.md) | Alpha, public beta, and REPS2 1.0 acceptance criteria |
| [Decision records](docs/decisions/) | Architecture decisions and research-backed behavior contracts |

## Engineering and legal boundaries

- REPS2 is an independent implementation. External projects may inform research into architecture and observable behavior; their source code is not copied or incorporated.
- Do not commit PS2 ISOs, BIOS dumps, proprietary SDKs, copyrighted game assets, or user-specific binaries.
- Local game validation must use lawfully obtained user-owned material and keep that material outside Git and CI.
- Unsupported instructions and hardware behavior must fail explicitly; silent approximations are not correctness claims.
- Behavior changes require tests and, where possible, differential evidence.

## License

REPS2 is licensed under **GNU General Public License v3.0 or later**. See [LICENSE](LICENSE).

Logo credits: [WinUI mark by Niels9001 (CC BY-SA 4.0)](https://commons.wikimedia.org/wiki/File:WinUI.svg), [Microsoft .NET brand assets](https://github.com/dotnet/brand/tree/main/logo), [CMake and .NET Core marks from Devicon (MIT)](https://github.com/devicons/devicon), and the [law scale icon from GitHub Primer Octicons (MIT)](https://primer.style/octicons/icon/law-16/).
