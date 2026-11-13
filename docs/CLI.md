# REPS2 command-line reference

The CLI is the developer and automation interface. This reference covers the command surface and session-scoped developer diagnostics.

## Commands

    reps2 [--dev] [--log-dir <directory>] <command> [command options]
    reps2 version
    reps2 disc-info <disc-image.iso>
    reps2 add-game <disc-image.iso> [--library <directory>]
    reps2 extract-boot-elf <disc-image.iso> <output.elf>
    reps2 recompile-iso <disc-image.iso> <new-output-directory> [--discover] [--tu-count N] [--probe-budget N] [--hle-syscalls] [--relocated-code DEST:SOURCE:SIZE]... [--max-blocks N] [--max-instructions N] [--max-roots N]
    reps2 elf-info <elf-file>
    reps2 analyze-elf <elf-file> [--discover] [--max-blocks N] [--max-instructions N] [--max-roots N]
    reps2 profile-elf <elf-file>
    reps2 recompile-elf <input-elf> <output.cpp> [--discover] [--tu-count N] [--native-build] [--parent-iso <disc.iso>] [--probe-budget N] [--hle-syscalls] [--relocated-code DEST:SOURCE:SIZE]... [--max-blocks N] [--max-instructions N] [--max-roots N]
    reps2 decode <instruction-word-hex> [pc-hex]
    reps2 compile-words <output.cpp> <base-pc-hex> <instruction-word-hex>...

    rg-reps2-engine <module.dll|module.so> [--ram <image.ram.bin>] [--media <disc.iso>] [--budget N] [--max-batches N] [--hle-syscalls] [--instruction-trace]

Example:

    reps2 elf-info "D:\List Games\some-owned-game\SLUS_123.45"
    reps2 recompile-elf "D:\List Games\some-owned-game\SLUS_123.45" build\game.cpp
    reps2 decode 0x2485FFFC 0x00100000
    reps2 compile-words output.cpp 0x00100000 0x24080005 0x24090007 0x01095021

## ISO-to-native preparation

`recompile-iso` is the one-command frontend path from an owned raw PS2 ISO to a generated native-module project. It reads the ISO through the bounded ISO 9660 reader, resolves `SYSTEM.CNF` with BOOT2 preferred over BOOT, extracts and validates the MIPS `ET_EXEC` boot ELF, and then invokes the existing recompiler with the original ISO bound as parent provenance. The command automatically enables `--native-build`; `--parent-iso` is implicit and cannot be overridden. The two `--relocated-code` ranges in the Downhill Domination example are title-specific evidence from the current USA boot-ELF investigation; do not copy them to other games or revisions without independently verifying their source and destination addresses.

```powershell
reps2 recompile-iso "D:/List Games/Downhill Domination.iso" "./build/downhill-domination" --discover --tu-count 8 --probe-budget 5000000 --hle-syscalls --relocated-code 80076000:2829F8:740 --relocated-code 80075000:2831A0:330
```

The output directory must not already exist. On success it contains:

- `boot.elf`: validated extracted SYSTEM.CNF boot target.
- `boot.ram.bin`: the generated exact 32 MiB initial EE main-RAM image.
- `boot.cpp` and any `boot.partNNNN.cpp` files: deterministic generated CFG translation units.
- `boot.manifest.json`: build inputs, SHA-256, analysis results, and `playable: false`.
- `boot.native/`: CMake project, host probe, and exported `rg_reps2_module.cpp` wrapper.

The native project still needs to be configured and built with the REPS2 SDK headers available:

```powershell
cmake -S ./build/downhill-domination/boot.native -B ./build/downhill-domination/boot.native/build -DREPS2_RUNTIME_INCLUDE="D:/REPS2-Project/runtime/include" -DREPS2_ENGINE_INCLUDE="D:/REPS2-Project/engine/include"
cmake --build ./build/downhill-domination/boot.native/build --config Release
$module = (Get-Content ./build/downhill-domination/boot.native/build/reps2-native-module-Release.path).Trim()
./build/Release/rg-reps2-engine.exe $module --ram ./build/downhill-domination/boot.ram.bin --media "D:/List Games/Downhill Domination.iso" --budget 5000000 --max-batches 16 --hle-syscalls
```

A failed extraction or recompile removes the incomplete output directory. Existing directories are never overwritten. The source ISO is read-only. This produces a bounded AOT module and runner input; it does **not** establish that the title boots or is playable. ELF relocations, unsupported R5900 instructions, IOP/GS/device behavior and other runtime requirements remain explicit blockers. The ISO's SHA-256 is checked against the exact extracted boot ELF and recorded in the generated native build identity.

## RG REPS2 ENGINE runner

The standalone `rg-reps2-engine` executable is the bounded native host for a module produced by `reps2 recompile-elf --native-build`. It uses the same public module loader and EngineSession APIs exposed by `RG::REPS2Engine`.

```powershell
cmake --build build --config Release --parallel
.\build\Release\rg-reps2-engine.exe "path\to\reps2_native_module.dll" --ram "path\to\game.ram.bin" --media "D:\List Games\owned-game.iso" --budget 5000000 --max-batches 16 --hle-syscalls
```

On Linux the executable is `build/rg-reps2-engine`; on Windows the default multi-config output is `build/Release/rg-reps2-engine.exe`. Installed builds place the runner in `bin`.

Arguments and execution contract:

- The first argument is an explicit generated native module path. The module descriptor, engine/runtime ABI, toolchain/layout fingerprint and target are checked before a session starts. Load only trusted modules: native code executes with the runner's process privileges and is not sandboxed.
- `--ram` is optional for synthetic modules. When supplied, the file must be exactly 33,554,432 bytes (32 MiB); it is copied into the session-owned EE main RAM before module initialization.
- `--media` installs the host-media callback for a non-empty raw PS2 ISO whose size is a multiple of 2048 bytes. Sector reads are range-checked and served from the host file; the runtime itself never opens files. Without this option, media operations fail closed.
- `--budget N` is the maximum guest instruction budget for each run batch (default 5,000,000). `--max-batches N` bounds the number of resumable batches (default 1, allowed 1–10,000). Execution stops earlier when the module returns a terminal guest status. A budget stop resumes the same session state in the next batch.
- `--hle-syscalls` enables only the currently implemented EE syscall HLE services. It does not imply that unimplemented kernel or IOP services work. `--instruction-trace` includes the runtime's bounded trace of up to 16 guest PCs.

The runner writes exactly one JSON object to stdout with kind `rg.reps2.engine.run_report.v1`. The report includes engine and module identity, native target, RAM/media inputs, session phase, final EE PC, total instructions executed, elapsed host microseconds, a `gprs` array with the 32 low 64-bit GPR lanes (`0x%016X`) for stop triage, a `timer3` object with the current `mode`, 16-bit `compare` and 16-bit `count` register state, the SIF/RPC observation object, per-batch budgets/status/PC/accounting and optional trace PCs. Guest stop statuses remain explicit: for example `unresolved_control_flow`, `unsupported_instruction`, `guest_exception` and `unhandled_syscall` are not reported as a completed or playable game. If the batch limit is reached while the guest is still budget-exhausted, `outcome` is `budget_limit_reached`. A zero process exit code means the host completed a bounded run and produced a report; it does not mean the game booted.

Host/argument/image/module/session errors use a structured `rg.reps2.engine.error.v1` JSON object and a non-zero exit code. Error codes are stable machine-readable identifiers. The JSON stream is suitable for an IDE or agent to capture without parsing human-oriented log text.

## Windows x64 installed toolchain

The project CMake install rules stage the Rust frontend, RG REPS2 ENGINE runner, static engine library, public headers and the MSVC C/C++ runtime redistributable DLLs:

```powershell
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix .\\dist\\rg-reps2-engine

.\\dist\\rg-reps2-engine\\bin\\reps2.exe version
.\\dist\\rg-reps2-engine\\bin\\rg-reps2-engine.exe --version
```

The install tree contains `bin/reps2.exe`, `bin/rg-reps2-engine.exe`, MSVC runtime DLLs in `bin/`, `lib/rg_reps2_engine.lib`, and the `include/rg_reps2_engine/` plus `include/reps2/runtime/` public headers. A separately generated game module is built by its emitted CMake project and supplied to the installed runner. The Windows runner uses the wide-character process entry point and converts command-line paths to UTF-8 before constructing filesystem paths; JSON path fields are emitted as UTF-8. This supports Unicode paths without relying on the active Windows ANSI code page.

CI validates the staged installation from outside the build directory, copies a frontend-generated DLL into a Unicode path containing spaces, loads and executes it through the staged runner, parses the JSON report, and compiles a separate C++ consumer using only the installed headers and static library. It then creates `rg-reps2-engine-windows-x64-msvc.zip`, ships the project licence, adds a versioned `bundle.json` (source commit plus engine and runtime ABI versions) and a UTF-8/LF per-file SHA-256 list, extracts it to a second Unicode path, rechecks every payload checksum (and refuses files the list does not cover), reruns the DLL through the relocated runner with the install tree removed from `PATH`, and builds a separate consumer against the extracted headers/library under the same `PATH` isolation. CI uploads the ZIP and its outer SHA-256 sidecar as the `rg-reps2-engine-windows-x64-msvc` artifact only when the smoke gate passes. The sample plugin inside the CI bundle is synthetic and explicitly marked non-playable. This is a developer SDK/toolchain bundle, not a polished game launcher or signed installer package.

## Developer diagnostics

Global developer options appear before the command:

    reps2 --dev version
    reps2 --dev --log-dir "D:\REPS2-Diagnostics" add-game "D:\List Games\owned-game.iso"
    reps2 --log-dir .\diagnostics disc-info "D:\List Games\owned-game.iso"

--dev enables session-scoped diagnostics in the per-user diagnostics directory. --log-dir selects an explicit root and also enables capture without requiring --dev. The default root is %LOCALAPPDATA%\REPS2\Logs on Windows, ~/Library/Logs/REPS2 on macOS, and $XDG_STATE_HOME/reps2/logs or ~/.local/state/reps2/logs on Linux. REPS2_LOG_DIR may override the default root.

Each command creates a unique session directory containing:

- session.log: one-line human-readable host-side events.
- events.jsonl: versioned structured events with a session identifier, monotonically increasing sequence, Unix timestamp, elapsed milliseconds, severity, category, subsystem, event name, message, and string-valued context.
- session.json: session metadata and final outcome (running, succeeded, or failed).
- summary.json: a compact structured summary (`kind: reps2.developer.session_summary`) with the outcome, event and diagnostic counts, each warning/error record's stable code, failure scope, and stage id, and the recorded invocation (program plus arguments) for reproduction. It is written at finalization, so it is unavailable for a session interrupted before then.

Session diagnostics record the frontend command lifecycle plus coarse-grained pipeline stages for analyze-elf, profile-elf, and recompile-elf. Stage events use pipeline.stage.started, pipeline.stage.succeeded, and pipeline.stage.failed, with stable stage IDs such as elf.parse, cfg.discover, ir.lift, and codegen.emit_cpp. Failed stages include REPS2_PIPELINE_STAGE_FAILED, failure_scope=pipeline_stage, their subsystem, duration, and the original error. This identifies a frontend stage, not an individual guest instruction or C++ runtime event. Runtime tracing remains a separate bounded facility so diagnostics cannot turn into per-instruction disk I/O. Error messages can contain local file paths; review diagnostic files before sharing them publicly.

## Agent and IDE developer CLI

The `dev` command namespace is intended for IDE integrations, coding agents, and automated triage. Its inspection commands write exactly one JSON report to stdout; they do not start a new diagnostics session, so inspecting the latest session cannot accidentally select itself.

    reps2 --dev --log-dir .\diagnostics version
    reps2 --log-dir .\diagnostics dev sessions
    reps2 --log-dir .\diagnostics dev sessions --limit 5
    reps2 --log-dir .\diagnostics dev inspect latest
    reps2 --log-dir .\diagnostics dev inspect session-1790000000000-1234-0
    reps2 --log-dir .\diagnostics dev summary latest
    reps2 --log-dir .\diagnostics dev compare latest session-1790000000000-1234-0
    reps2 dev capabilities

`dev sessions` returns recent session identifiers, manifest summaries, event counts, diagnostic counts, and integrity warnings. Its `count` is the number returned, `available_count` is the number discovered before applying `--limit`, and `truncated` tells an agent whether older sessions remain outside the response. `dev inspect` returns the raw session manifest as a JSON object, the event stream as a JSON array, warning/error events (excluding the duplicate terminal session summary) in a separate `diagnostics` array, and an `integrity_errors` array for missing or malformed artifacts. The default log root is the same platform-specific directory used by `--dev`; `--log-dir` can select the root and must appear before `dev`.

The report envelope is versioned with `schema_version: 1` and a `kind` discriminator:
- `reps2.developer.session_list`
- `reps2.developer.session_inspection`
- `reps2.developer.session_summary`
- `reps2.developer.session_comparison`
- `reps2.developer.capabilities`
- `reps2.developer.error`

The output contract is documented in [docs/schemas/developer-report-v1.schema.json](schemas/developer-report-v1.schema.json). Errors from the `dev` inspection commands use a JSON error envelope on stdout and a non-zero process exit code. Successful inspection commands exit with code 0. Human help is available through `reps2 dev help`.

`dev capabilities` emits one deterministic JSON document (`kind: reps2.developer.capabilities`) describing the contract so an IDE or agent need not hardcode it: the tool version, the report/event/session-manifest schema versions, the artifact file names, every envelope `kind`, the machine-readable `dev` command grammar, the stable diagnostic-code catalog, the log-root resolution policy, and a privacy note. It starts no diagnostics session and requires no `--log-dir`. The stable diagnostic codes (`REPS2_PIPELINE_STAGE_FAILED`, `REPS2_COMMAND_FAILED`, `REPS2_DEV_INVALID_ARGUMENTS`, `REPS2_DEV_SESSION_LIST_FAILED`, `REPS2_DEV_SESSION_INSPECTION_FAILED`, `REPS2_DEV_SUMMARY_UNAVAILABLE`) are defined once in `diagnostics::codes` and cataloged with their severity, scope, and surface; agents should match on these codes rather than on human-readable message text. See `docs/decisions/0086-developer-capability-discovery.md`.

`dev summary <latest|session-id>` echoes the session's `summary.json` as one JSON document (`kind: reps2.developer.session_summary`). It is the compact triage view: the session outcome, event and diagnostic counts, and each warning/error record as a structured object with the stable `code`, failure `scope`, and `stage_id` as first-class fields, so an agent does not have to parse the raw event stream to identify what failed. It also carries a `reproduction` object (`program` plus the exact `arguments` recorded at start) so the session can be re-run. A session that predates `summary.json`, or was interrupted before finalization, returns the `REPS2_DEV_SUMMARY_UNAVAILABLE` error envelope rather than a fabricated summary. See `docs/decisions/0090-structured-session-summary.md` and `docs/decisions/0093-reproduction-metadata.md`.

`dev compare <base> <candidate>` deterministically compares two session summaries and emits one JSON document (`kind: reps2.developer.session_comparison`). It reports whether the two sessions are `identical`, whether the `command` or `outcome` changed, and the `added`/`removed` failure signatures — where a signature is the stable `code|scope|stage_id|event` tuple. Message text is deliberately excluded so transient paths do not make otherwise-identical failures look different; multiplicities are preserved. This supports regression triage: re-run a known invocation and see exactly which failure categories appeared or disappeared. Reading a summary requires a JSON reader, so the frontend now has a small, bounded, fail-closed reader (`developer_json`) — it remains dependency-free and refuses malformed input rather than coercing it. See `docs/decisions/0094-differential-session-comparison.md`.

With `--dev` enabled, regular commands now emit `command.started`, `command.succeeded`, or `command.failed` events under the pipeline category. Each event identifies the top-level command subsystem (for example `iso9660`, `library`, `analysis`, `recompiler`, or `codegen`). Failures include the stable `REPS2_COMMAND_FAILED` code, command name, failure scope, and original error text. Stage-level attribution is available for ELF analysis, decoder coverage profiling, and native recompilation. Stable stage IDs identify input reads, ELF parsing/target validation, RAM-image creation, relocated-code preparation, CFG discovery, IR lifting, C++ generation, artifact writes, and native-project generation. The event stream still does not claim to identify an individual guest instruction or C++ runtime/device event. A session interrupted before finalization is returned as incomplete with explicit integrity warnings rather than being marked successful.

Do not scrape human CLI output when an agent needs a session report. Run the relevant command with `--dev`, then call `dev inspect latest` using the same diagnostics root. Review reports before sharing them because error text can include local paths.

## add-game

add-game accepts one raw 2048-byte-sector ISO 9660 image and stores a content-addressed record below the default per-user REPS2 library (`%LOCALAPPDATA%\\REPS2\\Library` on Windows). Set `REPS2_LIBRARY_DIR` or pass `--library <directory>` only to override the default. The generated `game.json` is the launcher-facing metadata contract; `boot.elf`, `recompiled.cpp`, and `ee-main-ram.bin` are stored only when the relevant stage succeeds. The original ISO is referenced by canonical path rather than copied, and duplicate imports are detected by content hash. The command is a managed import pipeline, not a claim that the imported title is playable.

## disc-info and extract-boot-elf

disc-info and extract-boot-elf accept raw 2048-byte-sector ISO 9660 images. The reader uses seeking and bounded directory/file reads, so a multi-gigabyte disc is not loaded into memory as a whole. extract-boot-elf resolves SYSTEM.CNF BOOT2 first and BOOT second, validates the target as an ELF32 little-endian EM_MIPS ET_EXEC, and creates a new output file without overwriting existing paths. Raw 2352-byte BIN/CUE tracks, CHD, UDF, Joliet/Rock Ridge names, multi-volume sets, multi-extent files, interleaved files, HDD PFS targets, and full PS2 optical drive emulation are not supported. The ISO is opened read-only; extraction writes only the separate output ELF.

## analyze-elf

`elf-info` and `analyze-elf` include the `elf_compat::analyze_compatibility()` metadata report: summary, severity, diagnostic code, explanation, and any blocked stage. These findings describe ELF metadata only; parsing or successful analysis is not proof that a title is compatible, boots, or is Playable. The command reports the API's blocked stages without duplicating its compatibility rules.

analyze-elf is a read-only, reachability-guided analysis command. It starts from the ELF entry point and executable STT_FUNC symbols, reports basic blocks and static edges, and explicitly lists indirect targets that cannot be resolved. The edge totals include a `syscall` continuation edge for every trap: the address a host service resumes at, which is not an architectural successor because an unserviced trap vectors through COP0. That continuation is queued leniently, since retail images also place data after traps, so an undecodable continuation is reported among the heuristic-only blocks that could not be decoded instead of failing the analysis (see `docs/decisions/0063-syscall-continuation-discovery.md`). It deliberately does not decode zero-filled BSS bytes. With `--discover`, it additionally scans code bytes for `JAL` call targets and canonical `addiu $sp, $sp, -N` stack prologues, and reads file-backed data words for code pointers (callback/vtable and syscall-handler tables). Code bytes and data words are separated by section-declared `SHF_EXECINSTR` ranges when the ELF provides them, falling back to the `PT_LOAD` `PF_X` flag otherwise, so a single writable+executable load segment is still scanned correctly (ADR 0070). Discovered roots are reported as `discovered`, and the output separates accepted roots, rejected candidates, and heuristic-only blocks that could not be decoded; discovery is a candidate heuristic, not a reachability or function-boundary proof. See `docs/decisions/0026-static-function-discovery.md`. Both analysis commands apply explicit safety bounds and accept `--max-blocks`, `--max-instructions` and `--max-roots` overrides. Defaults are 250,000 blocks, 2,000,000 decoded instructions and 500,000 discovered roots; overrides must be positive and stay below CLI ceilings above the physical 32 MiB address space. A block or instruction limit failure names the flag that raises it. See `docs/decisions/0030-analysis-limit-policy.md`.

## profile-elf

profile-elf is a read-only decoder census over every complete, naturally aligned 32-bit word wholly contained in the file-backed bytes of executable PT_LOAD segments. Partial words, unrelated bytes after a segment, and zero-filled BSS tails are not instructions. It reports supported, trap, and unsupported word counts, supported-opcode totals, and unsupported counts by encoding family and exact word. Because embedded data and unreachable bytes are included, the totals are an address-space census rather than a reachability claim. A separate bounded linear scan from the ELF entry point stops at the first unsupported instruction or trap and reports that address and word.

## recompile-elf

### Inputs and outputs

recompile-elf accepts a static little-endian MIPS ET_EXEC whose PT_LOAD segments all map into the documented default 32 MiB EE main RAM and whose allocated sections have no unapplied relocations. It writes the requested C++ source plus a companion file formed by replacing the output extension with ram.bin (for example, game.cpp and game.ram.bin). The binary is a raw 32 MiB initial RAM image with PT_LOAD bytes copied into physical main-RAM offsets and BSS zero-filled.

### Using the generated code

Generated C++ exports initialize_ee_state(EeState&, std::span<const std::uint8_t>), which validates both buffers, copies the image into caller-owned RAM, clears GPRs, and sets the PC to the ELF entry; it also exports execute_cfg(EeState&, instruction_budget). The caller must load the ram.bin file into a 32 MiB byte vector, attach a writable 32 MiB RAM span to EeState::memory.main_ram, call the generated initializer, then execute.

### Control flow and exceptions

Direct branches, J/JAL, JR/JALR and the supported REGIMM branches are represented with explicit delay slots. Unsupported instructions and out-of-graph control flow return structured statuses. Known `SYSCALL`/`BREAK` instructions now enter the EE Level-1 common exception vector: generated code updates COP0 Cause/EPC/Status (including nested EXL handling and BEV vector selection) and returns `Status::exception_delivered` as a host boundary. `ERET` is lowered as a dynamic control-flow terminator that resumes through EPC or ErrorEPC and clears the corresponding exception-level bit. This does not execute BIOS/HLE syscall services; other exception causes and ERET in a delay slot remain unsupported. This is a compiler vertical slice, not a complete game recompiler or runnable PS2 module.

### Discovery and translation units

With `--discover`, the same translation starts from the discovered candidate roots as well; stripping retail binaries yields thousands of candidates and a correspondingly large CFG. Every block is emitted as its own `block_0xXXXXXXXX` function dispatched through a sorted table. `--tu-count N` writes the dispatcher to the requested output and N block parts beside it as `<stem>.partNNNN.cpp` (stale parts from an earlier run are removed); the default of 1 keeps a single self-contained file. On Black/USA, `--discover --tu-count 8` produces a 15.84 MiB dispatcher plus eight 17.89-23.87 MiB parts that compile and link with MSVC `/O2 /W4 /permissive-` in about 149 s sequentially with zero warnings.

### Build manifest

recompile-elf also writes `<stem>.manifest.json` beside the translation: it records the manifest schema version, tool version and host target, the input path/size/SHA-256, analysis limits and discovery totals, program counts, the exact translation-unit and part layout, the RAM artifact size, and `playable: false`. Rendering is deterministic, so an identical invocation produces a byte-identical manifest.

### Native build project

`--native-build` writes a deterministic `<stem>.native/` project containing `CMakeLists.txt`, `probe_main.cpp`, and `rg_reps2_module.cpp`. CMake builds both the bounded host probe and a shared library named `reps2_native_module` (a `.dll` on Windows or `.so` on Linux). The generated wrapper exports `rg_reps2_get_native_module_descriptor_v1`, adapts the generated `initialize_ee_state` and `execute_cfg` functions to RG REPS2 ENGINE's versioned POD/C ABI, and uses a deterministic bounded module identity. The module calls into the exact translation units already compiled for the probe; no second guest-code generator or per-instruction host callback is introduced.

Configure the generated project with both public include roots:

```sh
cmake -S <stem>.native -B <build> \
  -DREPS2_RUNTIME_INCLUDE=<repo>/runtime/include \
  -DREPS2_ENGINE_INCLUDE=<repo>/engine/include
cmake --build <build> --config Release
ctest --test-dir <build> -C Release --output-on-failure
```

When using an installed SDK, point the two variables at the installed `include` directory that contains both `reps2/runtime/` and `rg_reps2_engine/`. CMake also writes `reps2-native-module-<config>.path` to report the exact shared-library output path. The module is intended for loading by an application linked with RG REPS2 ENGINE; its native code runs with the host process's privileges, so only load artifacts from trusted sources. Libraries are pinned for process lifetime; hot-unload is not supported.

The probe loads `<stem>.ram.bin` (or takes a path and instruction budget as arguments), calls `initialize_ee_state`, and executes with a bounded instruction budget. `--probe-budget N` bakes N into the generated probe and its CTest as the default budget; the default of 0 stops before any guest instruction runs, while a positive budget lets the generated CTest exercise real translated code.

For native provenance tied to a disc, use `--native-build --parent-iso <disc.iso>`. The CLI hashes the original ISO bytes using streaming reads, resolves and extracts its SYSTEM.CNF BOOT2/BOOT ELF using the ISO reader, and requires the extracted ELF's raw-byte SHA-256 to match the input ELF before generating artifacts. The verified original-ISO SHA-256 is bound to the input ELF by `render_with_game_identity()` and recorded as `parent_iso_sha256`. Without `--parent-iso`, standalone ELF workflows remain supported and ISO provenance remains unavailable (`null`); no disc identity is inferred.

The probe prints one machine-readable trace line: `entry`, `status` (the `Status` enumerator), final `pc`, `budget`, `executed` (guest instructions charged by the block-atomic dispatcher), and the COP0 `cause`/`epc`/`cop0_status`/`badvaddr` registers. Set `REPS2_INSTRUCTION_TRACE=1` to opt into bounded `instruction_trace_pcs` output (up to 16 guest PCs); tracing is disabled by default. The generated CTest uses a `PASS_REGULAR_EXPRESSION` on that line so it fails if the trace is lost.

`--hle-syscalls` enables the runtime EE syscall HLE layer in the generated probe (see `docs/decisions/0062-ee-syscall-hle-dispatch.md`). Without it a guest `syscall` delivers the architectural Level-1 exception exactly as before the layer existed. With it, the service numbered by `$v1` is looked up in the host table: a serviced call resumes after the trap (or at the branch target/fallthrough for a delay slot) and the trace gains `hle`, `syscalls`, `hle_handled`, `hle_pc`, `hle_number`, `hle_v0_before`, `hle_a0`, `hle_a1`, `hle_a2`, `hle_a3`, `hle_sp` and `hle_name`; a service number with no handler stops as `status=unhandled_syscall` with the PC still on the `syscall` and no COP0 context. The layer never returns success for a service it does not implement. The added register fields are host observations captured before dispatch (ADR 0065). The `CreateSema` boundary is reported separately: `sema_calls`, `sema_live`, `sema_param`, `sema_init`, `sema_max`, `sema_attr`, `sema_option`, `sema_id` and `sema_refused` describe the last CreateSema call, where `sema_id=-1` with a `sema_refused` reason means the service returned the documented failure code and allocated nothing (ADR 0066). The trace also carries `syscall_seq_count` and `syscall_seq`: up to 16 recent boundaries as `pc:number:path:a0:a1:a2:a3:v0` records joined by `;`, where `path` is `G` (registered guest handler), `H` (host service) or `U` (unhandled). See `docs/decisions/0071-syscall-sequence-trace.md`. SetSyscall registration is reported through the runtime state and never implies that the registered guest handler executed (ADR 0067).

The SIF/IOP boundary is reported with `sif_iop` (`absent` or `resident`), the device's `sif_smflag`/`sif_msflag` words, the EE-visible `sif_hw_subaddr` register, the `sif_get_regs`/`sif_set_regs`/`sif_set_dma` call counters, and `sif_cmd_pkts`/`sif_cmd_cid`/`sif_cmd_pktsize`/`sif_cmd_init` for the last SIFCMD command packet the modeled IOP device dispatched. These are host observations of the modeled device and do not imply real IOP execution; see ADR 0099. The inbound IOP-to-EE RPC init reply is reported by `sif_ee_inbound` (inbound packets dispatched), `sif_ee_inbound_cid` (last inbound command id), `sif_ee_sreg0` (the value observed at the guest's software register 0 after the reply), `sif_rpcinit_sent` and `sif_ee_sif0_event` (whether the SIF0 `D_STAT` event was published); see ADR 0100.

The generated files contain no absolute paths or timestamps.

When the generated native project is built, it also writes a configuration-specific `reps2-native-build-identity-<config>.json`. Its `cache_key_sha256` hashes the exact configuration-specific toolchain report. That report includes the source boot-ELF digest (null for synthetic fixtures), translated source digest, RAM-image digest when present, runtime ABI/header digest, IR and manifest schema versions, compiler/linker binary digests, hashed compiler-argument/launcher settings, target, CMake generator, configuration and compile/link flags. This is suitable as a future cache-key input; REPS2 does not yet reuse cached builds. The direct native-build path identifies the ELF, not the original ISO/disc image.

`--relocated-code DEST:SOURCE:SIZE` (hexadecimal, repeatable) analyses bytes that a title copies into RAM at run time. `SIZE` bytes are read from the initial EE main-RAM image at virtual address `SOURCE` and analysed as executable guest code at `DEST`; `DEST` must be word-aligned and `SIZE` a nonzero multiple of four. The region becomes an authoritative analysis root and is recorded in the manifest (version 3). The initial RAM image is unchanged, so the guest's own copy remains an observable action; code that is not declared still stops at that address as `unresolved_control_flow`. See `docs/decisions/0074-relocated-code-regions.md`.

### Related decisions

See `docs/decisions/0027-multi-translation-unit-codegen.md`, `docs/decisions/0028-deterministic-build-manifest.md` and `docs/decisions/0029-native-build-driver.md`. Analysis bounds and their overrides are documented in `docs/decisions/0030-analysis-limit-policy.md`. The initial guest-trap classification is documented historically in `docs/decisions/0031-guest-trap-status.md`; the current Level-1 exception/ERET behavior is specified in `docs/decisions/0033-ee-level1-exception-and-eret.md`. The EE syscall HLE dispatch and its `SetupThread`/`SetupHeap`/`CreateSema`/`FlushCache` services are specified in
`docs/decisions/0062-ee-syscall-hle-dispatch.md`,
`docs/decisions/0064-ee-setupheap-hle.md` and `docs/decisions/0066-ee-createsema-hle.md`.

## Development-only commands

compile-words remains a narrow straight-line development command. compile-cfg-fixture is an internal synthetic fixture generator used by CTest. Neither command consumes or distributes game data. `EeState::memory.main_ram` must be supplied by the host as a 32 MiB writable byte span before executing generated code that performs memory operations; the generated module does not allocate or own the console's RAM.
