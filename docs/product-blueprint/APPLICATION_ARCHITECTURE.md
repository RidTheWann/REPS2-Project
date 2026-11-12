# REPS2 Desktop Application Architecture

Version 1.0 (product blueprint). Companion documents: `INFORMATION_ARCHITECTURE.md`
(screen S-IDs, screen -> backend mapping), `COMPATIBILITY_UX_MODEL.md` (lifecycle
and evidence), `ENGINE_INTEGRATION_CONTRACT.md` (every cross-process call),
`IMPLEMENTATION_ROADMAP.md` (phasing). Repository truth lives in
`../ARCHITECTURE.md`, `../CLI.md`, `../PRODUCT_VISION.md`, `../ROADMAP.md`, and
`../../design-system/reps2/MASTER.md`.

Interface classification used throughout:
**[E]** existing, **[P]** partially implemented, **[PROP]** proposed,
**[N]** not implemented, **[UNKNOWN]** not verifiable from the repository.

## 1. Inherited constraints (verified, not designed here)

1. **Boundary is generated source + a machine-readable manifest, not FFI.**
   `../ARCHITECTURE.md` states the frontend emits C++ and metadata; there is no
   per-instruction Rust<->C++ bridge. A coarse C ABI is allowed only later for a
   measured in-process use case. The GUI therefore must not expect a Rust or C++
   library ABI to call. **[E]**
2. **The desktop shell calls the same library/build/session API as the CLI.**
   `../PRODUCT_VISION.md` (line 92) and `../ROADMAP.md` M7 require it. Today that
   API *is* the `reps2` and `rg-reps2-engine` process surface (`../CLI.md`). The
   GUI consumes it by **subprocess + versioned JSON**. **[E]**
3. **Native modules are pinned for process lifetime; there is no unload**
   (`../ARCHITECTURE.md`: hot-unload/`FreeLibrary`/`dlclose` unsupported; module
   loader pins for the process lifetime). Consequence: a UI that runs titles
   in-process could never release a game or recover from a guest stop. Each
   session must be its own child process. **[E]** constraint, **[PROP]** process
   design derived from it.
4. **Native modules are trusted code, not sandboxed** (`../ARCHITECTURE.md`,
   `../CLI.md`). The GUI must never load a generated module into the UI process,
   and must present the generated module as trusted native code. **[E]**
5. **No state may be invented.** `COMPATIBILITY_UX_MODEL.md` §1 fixes the
   artifact set the UI may read; "Unknown" is a first-class value. **[E]**

## 2. Layered architecture

```
+---------------------------------------------------------------------------+
| L5 PRESENTATION            WinUI 3 shell (S1..S13, INFORMATION_ARCHITECTURE)|
|                            views, view-models, focus/controller model      |
+---------------------------------------------------------------------------+
| L4 APPLICATION SERVICES    LibraryService  IsoIngestionService             |
|                            BuildJobManager CompatibilityStateManager       |
|                            EngineSessionManager DiagnosticsService          |
|                            SettingsStore  SaveStore                        |
|                            (own ALL subprocess + JSON + filesystem I/O)    |
+---------------------------------------------------------------------------+
| L3 JOB + STATE MANAGERS    build-job store (Builds/), state derivation,    |
|                            artifact index, event fan-out                   |
+---------------------------------------------------------------------------+
| L2 FRONTEND + RUNTIME       FrontendClient        EngineSessionManager     |
|    INTEGRATION              (spawn reps2.exe)     (spawn rg-reps2-engine)  |
|                            process/stdio/JSON adapters, no FFI             |
+---------------------------------------------------------------------------+
| L1 ENGINE + RUNTIME        rg_reps2_engine static lib + runner             |
|                            generated native module (trusted DLL)           |
|                            reps2 C++20 runtime (EE/mem/devices/HLE)        |
|                            OPAQUE to L4/L5 - never linked into the GUI     |
+---------------------------------------------------------------------------+
```

Rules:
- **L5 -> L4 only.** No screen touches a process, path, or JSON field directly
  (`INFORMATION_ARCHITECTURE.md` §4 states every screen goes through a service).
- **L4 owns the process/JSON boundary.** The GUI links neither `rg_reps2_engine`
  nor `reps2/runtime`, and never references `engine/include` or `runtime/include`.
- **L1 is opaque.** The GUI reads only the versioned JSON that L4 returns.

## 3. Layer responsibilities

| Layer | Owns | Must not |
|---|---|---|
| L5 Presentation | Screen composition, view-model binding, controller/keyboard focus, empty/loading/error presentation | Read files, spawn processes, parse JSON, decide compatibility state |
| L4 Application services | Subprocess invocation, stdout JSON parsing, artifact reads, job persistence, state derivation, settings/save storage | Embed engine headers, call IR/decoder, fabricate state |
| L3 Job + state managers | Durable build-job records under `Builds/`, artifact index, derived-state cache invalidation | Assume a later lifecycle stage from an earlier artifact |
| L2 Integration | Argument construction, stdio capture, exit-code mapping, Unicode path handling, cancellation/kill | Interpret semantics beyond the JSON contract |
| L1 Engine + runtime | Guest semantics, ABI admission, session execution, HLE | Be aware of the GUI at all |

## 4. Application service catalogue

| Service | Responsibility | Consumed interface (class) | Class |
|---|---|---|---|
| **LibraryService** | Enumerate/one-shot read managed library records; relink; integrity re-check | library directory `<library>/games/<sha256>/game.json` **[P]** (import exists; **no list/index API**) | P |
| **IsoIngestionService** | Validate one ISO and trigger import | `reps2 add-game <iso> [--library <dir>]` **[E]** | E |
| **BuildJobManager** | Run translate + native build as durable jobs; record per-stage outcome | `reps2 recompile-iso` / `reps2 recompile-elf --native-build` **[E]**; `cmake` configure/build **[E]**; identity JSON **[E]**; job store **[N]** | E/PROP |
| **CompatibilityStateManager** | Derive the lifecycle tuple from artifacts only | `game.json`, `<stem>.manifest.json`, identity JSON, engine run report, `reps2 dev` report **[E]**; derivation layer **[PROP]** | PROP |
| **FrontendClient** | Single choke point for spawning `reps2.exe`; parses stdout; maps exit codes | `reps2` process + human stdout + `--dev` events **[E]** | E |
| **EngineSessionManager** | Spawn/observe/stop one `rg-reps2-engine` process per session; parse run report | `rg-reps2-engine <module> [--ram|--media|--budget|--max-batches|--hle-syscalls|--instruction-trace]` **[E]**; in-process session control **[N]** | E/N |
| **DiagnosticsService** | Index diagnostics sessions, surface structured failures | `reps2 dev sessions\|inspect\|summary\|compare\|capabilities` **[E]** | E |
| **SettingsStore** | Persist app settings (paths, runtime defaults, input, logging, dev mode) | app config file **[N]** | N |
| **SaveStore** | Manage per-game save paths | runtime save format **[N]**/**[UNKNOWN]** | N |

### 4.1 LibraryService
- **Responsibility:** own the managed library directory, enumerate records, verify
  integrity (`game.json` digests vs `boot.elf`), and expose relink. Relink must
  rebind only to the same ISO digest (`COMPATIBILITY_UX_MODEL.md` §3
  "Recovery required").
- **Consumed interface:** the documented content-addressed layout
  `<library>/games/<sha256>/{game.json,boot.elf,recompiled.cpp,ee-main-ram.bin}`
  (`../ARCHITECTURE.md`, `rust/frontend/src/library.rs`). **There is no
  `reps2 library list` command today**; the app reads the directory. **[P]**
- **Class:** P (import/idempotency exist; index and relink do not).

### 4.2 IsoIngestionService
- **Responsibility:** take one user-selected ISO path and hand it to the import
  pipeline; never extract files manually, never ask for BOOT.ELF.
- **Consumed interface:** `reps2 add-game <iso> [--library <directory>]` **[E]**;
  library root resolution via `REPS2_LIBRARY_DIR` or default
  `%LOCALAPPDATA%\REPS2\Library` **[E]**.
- **Class:** E.

### 4.3 BuildJobManager
- **Responsibility:** orchestrate the multi-step build as one tracked job with
  per-stage outcome, and persist it under `Builds/`. It drives translation and
  the host compile/link, holding the generated project's declared output path.
- **Consumed interface:** `reps2 recompile-iso` / `recompile-elf --native-build`
  **[E]**; the emitted CMake project `<stem>.native/` **[E]**; `cmake -S ... -B ...`
  + `cmake --build` **[E]**; `reps2-native-build-identity-<config>.json` **[E]**;
  durable job store **[N]**.
- **Class:** E (commands) / [N] (durable queue; `INFORMATION_ARCHITECTURE.md`
  §4 marks S7 Build Queue as PROP).

### 4.4 CompatibilityStateManager
- **Responsibility:** compute the tuple
  `(stage, evidence_refs[], blockers[], allowed_actions[], disabled_actions[], recovery[])`
  from `COMPATIBILITY_UX_MODEL.md` §1 artifacts. Never promotes a stage without
  the named evidence; regresses explicitly.
- **Consumed interface:** artifacts only (see §7). **The derivation layer does
  not exist today.** **[PROP]**
- **Class:** PROP.

### 4.5 FrontendClient
- **Responsibility:** the only place that spawns `reps2.exe`; builds argv,
  captures stdout/stderr, maps the process exit code, and validates the JSON it
  is allowed to parse. Resolves the installed `bin/reps2.exe` from the bundle.
- **Consumed interface:** `reps2` CLI surface (`../CLI.md`) **[E]**.
- **Class:** E (commands) / [PROP] (typed adapter).

### 4.6 EngineSessionManager
- **Responsibility:** own one `rg-reps2-engine` child process per session; pass
  `--ram/--media/--budget/--max-batches/--hle-syscalls/--instruction-trace`; parse
  the run-report JSON; expose Play/Stop; guarantee clean termination.
- **Consumed interface:** `rg-reps2-engine` runner **[E]**; run report
  `rg.reps2.engine.run_report.v1` **[E]**. **There is no in-process session
  control API and no way to pause/step a running process other than the
  `--max-batches` batching loop inside one invocation**; Stop = terminate the
  child. **[N]** for fine-grained control.
- **Class:** E (runner) / [N] (fine-grained session control).

### 4.7 DiagnosticsService
- **Responsibility:** index diagnostics sessions and surface the stable failure
  codes and pipeline stage IDs to the Developer screens (S9/S10/S12).
- **Consumed interface:** `reps2 dev sessions|inspect|summary|compare|capabilities`
  **[E]**; `developer-report-v1.schema.json` (schema_version 1),
  kinds `reps2.developer.*` **[E]**.
- **Class:** E.

### 4.8 SettingsStore
- **Responsibility:** persist user settings. Nothing exists today.
- **Consumed interface:** none. **[N]**

### 4.9 SaveStore
- **Responsibility:** per-game save paths and (later) save data.
- **Consumed interface:** the runtime has no save/memory-card implementation
  (`../ROADMAP.md` M5), so the save format is **[UNKNOWN]**. **[N]**

## 5. Process model

| Process | Role | Lifetime | Notes |
|---|---|---|---|
| **REPS2 GUI** (WinUI 3) | Presentation + L4 services + L3 managers | User session | Never loads a generated module |
| **`reps2.exe`** child | Import / analysis / translate / build orchestration | One command per invocation | `add-game`, `recompile-iso`, `dev ...`; exit 0 or 1 |
| **`cmake`** child tree | Host compile/link of the emitted project | Per build job | Driven by BuildJobManager; MSVC toolchain |
| **`rg-reps2-engine.exe`** child | Bounded native session | Per session | Loads the trusted module; exit 0/2/3/4/5/6/10 |

Rules:
- **One child process per command.** No long-lived `reps2` daemon exists.
- **One session process per game.** Termination frees RAM and the pinned module;
  nothing is unloaded inside the UI.
- **stdio contract.** The engine writes exactly one JSON object to stdout
  (`../CLI.md`); `reps2` writes human text to stdout and errors to stderr, and
  JSON only for `dev` commands. The GUI must not scrape human CLI text
  (`../CLI.md` "Do not scrape human CLI output").
- **Unicode paths.** The runner already crosses a wide-char entry point and emits
  UTF-8 JSON paths (`../CLI.md`); the GUI must pass paths untouched and treat
  JSON path fields as UTF-8.
- **No `PATH`/cwd dependencies.** The GUI resolves child executables by absolute
  installed path.

## 6. Threading and async model

- **UI thread** renders only; all service calls are `Task`-based and awaited.
- **Process I/O** (stdout/stderr drain) runs on background tasks; large output is
  streamed, not buffered whole.
- **Job manager** polls job state on a timer and raises change events; the UI
  never blocks the dispatcher on a build.
- **State derivation** is pure over artifact bytes; it runs off-thread and its
  result is cached keyed by `(record mtime set, run-report digest)`.
- **Cancellation:** cancelling a build kills the `cmake`/`reps2` child tree;
  cancelling a session kills the `rg-reps2-engine` child. Because the module is
  pinned, killing the process is the supported stop.
- **Concurrency limit:** the UI allows **one active session process**, but N
  concurrent build jobs bounded by a small pool (host compile is memory-heavy;
  the Black/USA translation is ~178 MB as one unit per `../ROADMAP.md`).

## 7. State model derivation

The state is a pure function of the artifacts in `COMPATIBILITY_UX_MODEL.md` §1.
No field is authored by the GUI.

| Derived stage | Artifact requirement (all must hold) | Producing command | Class |
|---|---|---|---|
| Imported | `game.json` with valid ISO SHA-256 + boot ELF SHA-256 | `reps2 add-game` | E |
| Analyzed | analysis coverage counts recorded for the exact boot ELF digest | `reps2 analyze-elf` / `add-game` manifest block | E |
| Translated | generated `.cpp` + manifest recorded, `playable:false` | `reps2 recompile-iso` / `recompile-elf` | E |
| Native-built | identity `cache_key_sha256` present + successful link | emitted CMake build | E |
| Boot-tested | run report for the exact module, repeatable stage, non-`budget_exhausted` | `rg-reps2-engine` | E |
| Playable | boot-tested **plus** a passing gameplay plan | none today | N |

**Current reality the UI must render (`COMPATIBILITY_UX_MODEL.md` §2, `../ROADMAP.md` M4):**
no title is Boot-tested or Playable. Downhill Domination USA reaches Native-built;
its latest executed bounded probe (ADR 0119 / PR #68, from `origin/main` `6756d0e`,
Windows x64 MSVC 19.51.36260.0) runs 4,999,998 of a 5,000,000 budget and stops
`budget_exhausted` at `pc=0x00246DAC`, spinning in a `PollSema` (0x45) loop on
semaphore id 5 — a waiting-on-an-unmodelled-producer boundary, not an
unimplemented service. The earlier `semaphore_wait_blocked` on `WaitSema` (0x44)
at `0x0025A724` (ADR 0101) is historical. Black USA is `NOT TESTED`. Play is
disabled everywhere.

## 8. Persistence layout

```
%LOCALAPPDATA%\REPS2\
  Library\        [E]  CLI-managed, content-addressed (games\<sha256>\game.json)
  Logs\           [E]  CLI diagnostics sessions (session.log, events.jsonl,
                       session.json, summary.json)
  Builds\         [PROP] app-managed build-job records + native project trees
  Config\         [N]  app settings (SettingsStore)
  Saves\          [N]  per-game save storage (SaveStore)
```

- `Library\` and `Logs\` follow the CLI's documented defaults (`../CLI.md`,
  `rust/frontend/src/main.rs` `default_library_root`). The GUI must not relocate
  them; it passes `--library`/`--log-dir` only when the user overrides.
- `Builds\`, `Config\`, `Saves\` are new, app-owned, and must never be inside
  `Library\` (which the CLI owns and validates by digest).

## 9. Security and hygiene

- **No extra copies.** The GUI adds no copy of the ISO or the boot ELF beyond the
  existing library record: the ISO stays at its canonical source path (referenced
  by path only), and the library's extracted `boot.elf` **[E]** is derived user
  data under the user profile. The GUI never writes inside `Library\games\*`.
- **No BIOS, no proprietary SDK, no game assets** are ever fetched, stored, or
  bundled (`../PRODUCT_VISION.md`, `../ROADMAP.md`).
- **Trust boundary.** A generated module is trusted native code (`../CLI.md`).
  The GUI presents it as such, loads it only in the runner child, and never
  claims sandboxing.
- **Path handling.** Canonicalize before hashing/storing; reject path traversal in
  any user-supplied name; pass paths within the process argv, not through shell
  interpolation; treat all JSON path fields as UTF-8.
- **Diagnostics privacy.** Error text can contain local paths (`../CLI.md`); the
  GUI must warn before the user exports/shares a diagnostics bundle.

## 10. GUI technology decision (Session 2 GUI study)

| Option | Fit for this product | Cost | Risk |
|---|---|---|---|
| **WinUI 3 (Windows App SDK) - chosen** | Windows-first matches the only validated target; GPU composition for a 60 fps grid; UIA + `Windows.Gaming.Input` satisfy controller-first and accessibility; integrates by subprocess + versioned JSON (no FFI, no engine change) | One new .NET toolchain lane in CI; Windows App SDK packaging | Newest of the three in team experience; packaging/runtime distribution must be solved in Phase 4 |
| WPF (.NET) - fallback | Same .NET toolchain lineage; mature; controller support via XInput interop | Weaker composition/graphics path; older control set | Lower ceiling for a Big-Picture-style shell |
| Tauri - alternative if .NET must be avoided | Web UI + Rust host; reuses the repo's Rust posture; small binaries | Adds a WebView2 runtime dependency; input/controller integration is weaker; larger surface for a native-feeling shell | Two UI stacks (web + native toolchain) diverge from the Windows-first goal |

**Decision:** PRIMARY WinUI 3; FALLBACK WPF; alternative Tauri only if .NET must be
avoided. The decisive constraint is that the engine integration is
**subprocess + JSON**, so the choice is reversible: no UI option requires an
engine or recompiler change.

## 11. Open questions / interfaces not verified

- **Library index API:** no `reps2 library list` / index command exists; the GUI
  must scan the documented directory layout. Whether the frontend team adds a
  machine-readable index command is **[UNKNOWN]**.
- **Session stop control:** the runner is a bounded one-shot batch loop; only
  process termination stops a session. Any in-process stop/pause is **[N]**.
- **Save format:** no runtime save/memory-card implementation exists; SaveStore
  is **[UNKNOWN]** pending M5.
- **Installed layout of the SDK bundle:** `../CLI.md` describes the installed
  `bin/reps2.exe` + `bin/rg-reps2-engine.exe` tree; the exact packaging the GUI
  ships is **[PROP]** (Phase 4).
