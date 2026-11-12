# REPS2 Engine Integration Contract

Version 1.0 (product blueprint). The GUI links neither `rg_reps2_engine` nor the
C++ runtime and calls **no** Rust or C++ function. Every interaction is a child
process whose argv carries inputs and whose stdout carries one versioned JSON
document. Sources of truth: `../CLI.md`, `../ARCHITECTURE.md`,
`engine/src/main.cpp`, `engine/include/rg_reps2_engine/*`,
`rust/frontend/src/library.rs`, `rust/frontend/src/native_build.rs`,
`docs/schemas/developer-report-v1.schema.json`. Screen references use the S-IDs in
`INFORMATION_ARCHITECTURE.md`; classification matches `APPLICATION_ARCHITECTURE.md`.

Classification: **[E]** existing, **[P]** partial, **[PROP]** proposed,
**[N]** not implemented, **[UNKNOWN]**.

## 1. Interaction inventory

| # | Interaction | Producer | Class | Consumed by |
|---|---|---|---|---|
| C1 | `reps2 add-game <iso> [--library]` | Rust CLI | E | IsoIngestionService (S3/S4) |
| C2 | `reps2 recompile-iso` / `recompile-elf --native-build` | Rust CLI | E | BuildJobManager (S4) |
| C3 | CMake configure + build of `<stem>.native/` | host toolchain | E | BuildJobManager (S4) |
| C4 | `rg-reps2-engine <module> [opts]` | C++ runner | E | EngineSessionManager (S8) |
| C5 | `reps2 dev <...>` reports | Rust CLI | E | DiagnosticsService (S9/S10) |
| C6 | `reps2 analyze-elf` / `recompile-elf` stage events | Rust CLI | E | DiagnosticsService (S9) |
| C7 | Artifact reads: `game.json`, manifest, identity | filesystem | E | CompatibilityStateManager |
| C8 | Exit-code taxonomy | both CLIs | E | FrontendClient / EngineSessionManager |

## 2. Per-interaction contracts

### C1 - `reps2 add-game`

- **Invocation [E]:** `reps2 add-game <disc-image.iso> [--library <directory>]`.
  Global `--dev` / `--log-dir` may precede the command.
- **Input:** one raw 2048-byte-sector ISO 9660 image; optional library root
  (`REPS2_LIBRARY_DIR` when `--library` is absent).
- **Output:** human-readable lines on stdout (title, game ID, game directory,
  boot ELF path, generated C++ yes/no, RAM image yes/no, "Playable: no"). It is
  **not** JSON; the authoritative result is the published record on disk.
- **On-disk effect [E]:** `<library>/games/sha256-<iso-digest>/{game.json,boot.elf}`
  plus `recompiled.cpp` / `ee-main-ram.bin` when those stages succeed. Staging +
  atomic rename; the source ISO is untouched.
- **Error surface:** non-zero exit (1) with `Error: ...` on stderr; `--dev`
  records `REPS2_COMMAND_FAILED` with the command name and scope.
- **Idempotency [E]:** re-importing unchanged content returns the existing record
  (`already_imported`); a tampered stored `boot.elf` fails the integrity check
  and nothing is overwritten.
- **Evidence class:** Observed (a record exists). Import is **not** analysis,
  translation, build, or boot evidence (`COMPATIBILITY_UX_MODEL.md` §2).
- **UI MAY:** show "Imported"; read `game.json` for identity.
- **UI MUST NOT:** infer Analyzed/Translated from import success; infer playability
  (`game.json` always carries `playable:false`,
  `playability_status:"blocked_runtime_incomplete"`).

### C2 - `reps2 recompile-iso` / `recompile-elf --native-build`

- **Invocation [E]:**
  - `reps2 recompile-iso <disc.iso> <new-output-dir> [analysis flags]`
  - `reps2 recompile-elf <input-elf> <output.cpp> --native-build [--parent-iso <disc.iso>]`
- **Input:** a raw ISO (iso path) or an eligible static MIPS `ET_EXEC`; limits
  `--max-blocks/--max-instructions/--max-roots`; `--discover`, `--tu-count N`,
  `--probe-budget N`, `--hle-syscalls`, `--relocated-code DEST:SOURCE:SIZE`.
- **Output [E]:** output directory containing `boot.elf`, `boot.ram.bin`,
  `boot.cpp` (+ `boot.partNNNN.cpp`), `boot.manifest.json`, `boot.native/`
  (`CMakeLists.txt`, `probe_main.cpp`, `rg_reps2_module.cpp`). `recompile-iso`
  forces `--native-build` and implicit `--parent-iso`; `--parent-iso` must not be
  supplied to `recompile-iso`.
- **Schema:** `<stem>.manifest.json` is build-manifest **schema v3**
  (`rust/frontend/src/build_manifest.rs` `MANIFEST_VERSION = 3`): input
  path/size/SHA-256, host target, applied limits, discovery totals, TU/part
  layout, RAM artifact, `playable:false`. Deterministic, timestamp-free.
- **Error surface:** non-zero exit (1); `recompile-iso` removes its incomplete
  output directory; an existing output directory is refused.
- **Idempotency [E]:** identical invocation -> byte-identical manifest. The output
  directory must not pre-exist, so re-runs need a fresh directory.
- **Evidence class:** Observed. Manifest success proves translation, not build or
  boot.
- **UI MAY:** show "Translated" and disclose `unsupported`/`unresolved` counts if
  surfaced (they are recorded in analysis blocks).
- **UI MUST NOT:** treat manifest existence as Native-built or Boot-tested.

### C3 - generated CMake native build

- **Invocation [E]:**
  `cmake -S <stem>.native -B <build> -DREPS2_RUNTIME_INCLUDE=<...> -DREPS2_ENGINE_INCLUDE=<...>`
  then `cmake --build <build> --config Release`.
- **Input:** the emitted project plus the two include roots (SDK or repo).
- **Output [E]:** a shared module (`reps2_native_module.dll`/`.so`), a path file
  `reps2-native-module-<config>.path`, a probe executable + CTest, and
  `reps2-native-build-identity-<config>.json`:
  `{schema_version:1, kind:"reps2.native_build_identity", cache_key_sha256, toolchain_report}`
  where `cache_key_sha256` hashes the config-specific `reps2-native-toolchain-<config>.json`.
- **Error surface:** CMake/MSVC diagnostics on stderr; non-zero CMake exit;
  identity file absent on failure.
- **Idempotency [E]:** deterministic project; build output is cache-keyed by the
  identity hash.
- **Evidence class:** Observed (link + identity). Not boot evidence.
- **UI MAY:** show "Native-built"; display the ABI/target and `cache_key` prefix.
- **UI MUST NOT:** treat a linked module or a passing synthetic probe as a
  boot-tested or Playable title.

### C4 - `rg-reps2-engine` session run

- **Invocation [E]:**
  `rg-reps2-engine <module.dll|module.so> [--ram <img.ram.bin>] [--media <disc.iso>]
  [--budget N] [--max-batches N] [--hle-syscalls] [--instruction-trace]`.
  `--ram` must be exactly 33,554,432 bytes. `--budget` default 5,000,000.
  `--max-batches` default 1, range 1..10000.
- **Input:** explicit module path; optional exact-size RAM image; optional raw ISO.
- **Output [E]:** exactly one JSON object on stdout, `kind:"rg.reps2.engine.run_report.v1"`:
  `engine{name,version,abi_version}`, `module{name,path,target{architecture,operating_system}}`,
  `input{ram_image,ram_bytes,media_image,media_sectors}`, `config{instruction_budget_per_batch,max_batches,hle_syscalls,instruction_trace}`,
  `outcome` (`completed`|`guest_stopped`|`budget_limit_reached`|`engine_error`),
  `session{phase,pc,pc_hex,instructions_executed,run_batches,elapsed_microseconds,media_present,syscalls_seen,syscalls_handled}`,
  `runs[]` (per-batch `guest_status`, `guest_status_code`, budget/PC/accounting,
  `invoked`, `monotonic_counter`), and `instruction_trace_pcs[]` (only with
  `--instruction-trace`, up to 16).
- **Error surface [E]:** `kind:"rg.reps2.engine.error.v1"` with `{code,message[,
  platform_error][,engine_error]}` and exit codes: 2 invalid args, 3
  RAM/media invalid, 4 module load failed, 5 session start failed, 6 execution
  error, 10 unhandled host exception. Exit 0 means a bounded run produced a
  report - **not** that a game booted.
- **Idempotency:** a session is stateful; repeated runs reproduce only for
  deterministic module+inputs. `--max-batches > 1` resumes budget stops inside
  one process.
- **Guest status vocabulary [E]:** runtime `Status` values including `ok`,
  `instruction_budget_exhausted`, `external_control_flow`,
  `unsupported_instruction`, `unhandled_syscall`, `guest_exception`
  (`exception_delivered`), `semaphore_wait_blocked`, `unresolved_control_flow`.
- **Evidence class:** Observed for execution; a single report is **not** Boot-tested
  (that requires a repeatable, non-`budget_exhausted` guest-visible stage,
  `COMPATIBILITY_UX_MODEL.md` §2).
- **UI MAY:** show "Run test session"; surface the exact stop PC and status.
- **UI MUST NOT:** map a non-zero guest stop to Play, or a `budget_limit_reached`
  outcome to success. **Stop** is implemented as terminating the child process:
  there is no in-process stop/pause/step API **[N]**.

### C5 - `reps2 dev` reports

- **Invocation [E]:** `reps2 [--log-dir <root>] dev sessions|inspect|summary|compare|capabilities`
  (`--log-dir` must precede `dev`).
- **Input:** the diagnostics root; a session id or `latest`.
- **Output [E]:** one JSON document per invocation, `schema_version:1`, kinds
  `reps2.developer.session_list|session_inspection|session_summary|session_comparison|capabilities|error`,
  validated by `docs/schemas/developer-report-v1.schema.json`. `dev capabilities`
  is the deterministic self-description an IDE/agent should read instead of
  hardcoding grammar. Stable codes include `REPS2_PIPELINE_STAGE_FAILED`,
  `REPS2_COMMAND_FAILED`, `REPS2_DEV_*`.
- **Error surface:** `reps2.developer.error` envelope on stdout with non-zero
  exit; successful inspection exits 0.
- **Idempotency [E]:** inspection does not start a new session, so `latest`
  cannot select itself.
- **Evidence class:** Observed (host-side frontend diagnostics). It is **not**
  guest/runtime evidence (`../CLI.md`).
- **UI MAY:** list sessions, show failure codes and stage IDs (S9/S10/S12).
- **UI MUST NOT:** present diagnostics as guest behavior, or parse human text
  where a `code`/`stage_id` field exists.

### C6 - pipeline stage events

- **Invocation [E]:** run a tracked command with `--dev`/`--log-dir`, then
  `reps2 dev inspect latest`.
- **Output [E]:** `events.jsonl` events with `category:pipeline`, names
  `pipeline.stage.started|succeeded|failed`, stable stage IDs (`elf.parse`,
  `cfg.discover`, `ir.lift`, `codegen.emit_cpp`, `iso.extract_boot_elf`, ...).
- **Error surface:** failed stage carries `REPS2_PIPELINE_STAGE_FAILED`,
  `failure_scope:pipeline_stage`, the stage id, duration, and original error.
- **Idempotency:** session-scoped; a new run is a new session.
- **Evidence class:** Observed frontend stage attribution - not per-instruction.
- **UI MAY:** show progress granularity equal to the stage IDs; never invent a
  finer percentage (`COMPATIBILITY_UX_MODEL.md` §7).
- **UI MUST NOT:** claim per-instruction or C++ runtime events from this stream.

### C7 - artifact reads

- **Input:** filesystem only.
- **Schemas [E]:**
  - `game.json` - library manifest schema v1 (`rust/frontend/src/library.rs`):
    `schema_version`, `game_id`, `display_name`, `serial_candidate`,
    `source_iso{path,size_bytes,sha256,copied_into_library}`, `disc{...}`,
    `boot{...elf{...}}`, `analysis{status, counts...}`,
    `native_build{generated_cpp, generated_ram_image, playable:false,
    playability_status}`, `artifacts{...}`.
  - `<stem>.manifest.json` - build manifest schema v3.
  - `reps2-native-build-identity-<config>.json` - `kind:reps2.native_build_identity`.
  - engine run report / developer report (above).
- **Error surface:** missing/invalid artifact = "Unknown", not a promoted stage.
- **Idempotency:** reads are pure.
- **Evidence class:** Observed.
- **UI MAY:** derive the lifecycle tuple.
- **UI MUST NOT:** write into `Library\games\*`, or treat `copied_into_library:false`
  as permission to move the ISO.

### C8 - exit-code taxonomy

| Process | Code | Meaning |
|---|---|---|
| `reps2` | 0 | command succeeded |
| `reps2` | 1 | command failed (`Error:` on stderr) |
| `rg-reps2-engine` | 0 | bounded run completed, report on stdout |
| `rg-reps2-engine` | 2 | invalid arguments |
| `rg-reps2-engine` | 3 | RAM or media image invalid |
| `rg-reps2-engine` | 4 | module load failed (descriptor/ABI/target) |
| `rg-reps2-engine` | 5 | session start failed |
| `rg-reps2-engine` | 6 | execution error / batch failure |
| `rg-reps2-engine` | 10 | unhandled host exception |

The GUI maps codes to recovery UI but always prefers the JSON `error.code`. A
zero exit is **never** proof of a guest-visible stage.

## 3. Proposed application-service API surface [PROP]

This is the interface the GUI would program against. Every row is proposed; none
exists as an in-process API today. Each is realized by wrapping the C1..C8
interactions above.

| Service method | Backing interaction | Class of backing |
|---|---|---|
| `LibraryService.ListRecords()` | scan `Library\games\*\game.json` (C7) | P |
| `LibraryService.ImportGame(isoPath)` | C1 | E |
| `LibraryService.VerifyRecord(gameId)` | C7 digest check | P |
| `LibraryService.Relink(gameId, isoPath)` | C1 idempotent re-import (same digest only) | P |
| `BuildJobManager.StartBuild(gameId, options)` | C2 then C3 | E |
| `BuildJobManager.ObserveJob(jobId)` | job store + C6 | PROP |
| `BuildJobManager.CancelJob(jobId)` | kill child tree | PROP |
| `CompatibilityStateManager.Derive(gameId)` | C7 (+C4/C5) | PROP |
| `EngineSessionManager.StartSession(gameId, sessionConfig)` | C4 | E |
| `EngineSessionManager.StopSession(sessionId)` | terminate child | PROP |
| `EngineSessionManager.StreamRunReport(sessionId)` | C4 stdout | E |
| `DiagnosticsService.ListSessions()` | C5 | E |
| `DiagnosticsService.InspectSession(id)` | C5 | E |
| `DiagnosticsService.SummarizeSession(id)` | C5 | E |
| `DiagnosticsService.CompareSessions(a, b)` | C5 | E |
| `SettingsStore.Load()/Save(settings)` | none (new store) | N |
| `SaveStore.ResolveSavePath(gameId)` | none (runtime saves absent) | N |

## 4. Stability and schema rules

- **Version gates.** `engine.abi_version` (2), descriptor ABI (1), runtime ABI,
  IR/manifest schema versions, and `schema_version` fields are contract surfaces.
  The GUI must reject mismatches and surface `engine_error`/
  `runtime_abi_mismatch` rather than retry blindly.
- **Determinism.** Manifests and identity files are timestamp-free; the GUI must
  not inject timestamps into cached fragments it compares.
- **No new FFI.** Any future in-process API must be coarse-grained POD/opaque
  handles (`../ARCHITECTURE.md`); the GUI blueprint assumes subprocess + JSON and
  does not depend on such a bridge existing.

## 5. Interfaces not verified

- **Library index command:** no `reps2 library list`; the directory scan in C7 is
  the only path today. **[UNKNOWN]** whether the frontend adds one.
- **Session stop/pause:** no in-process control; Stop = kill child. **[N]**.
- **Save format:** no runtime save/memory-card support. **[UNKNOWN]**.
- **Structured build-job stream:** `reps2` job output is human text; only `dev`
  reports are structured. A streaming job API is **[PROP]**.
- **Gameplay acceptance output:** no command produces a "Playable" verdict; the
  Playable state has no producer today. **[N]**.
