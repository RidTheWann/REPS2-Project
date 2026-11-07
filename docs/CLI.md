# REPS2 command-line reference

The CLI is the developer and automation interface. This reference covers the command surface and session-scoped developer diagnostics.

## Commands

    reps2 [--dev] [--log-dir <directory>] <command> [command options]
    reps2 version
    reps2 disc-info <disc-image.iso>
    reps2 add-game <disc-image.iso> [--library <directory>]
    reps2 extract-boot-elf <disc-image.iso> <output.elf>
    reps2 elf-info <elf-file>
    reps2 analyze-elf <elf-file> [--discover] [--max-blocks N] [--max-instructions N] [--max-roots N]
    reps2 profile-elf <elf-file>
    reps2 recompile-elf <input-elf> <output.cpp> [--discover] [--tu-count N] [--native-build] [--probe-budget N] [--hle-syscalls] [--relocated-code DEST:SOURCE:SIZE]... [--max-blocks N] [--max-instructions N] [--max-roots N]
    reps2 decode <instruction-word-hex> [pc-hex]
    reps2 compile-words <output.cpp> <base-pc-hex> <instruction-word-hex>...

Example:

    reps2 elf-info "D:\List Games\some-owned-game\SLUS_123.45"
    reps2 recompile-elf "D:\List Games\some-owned-game\SLUS_123.45" build\game.cpp
    reps2 decode 0x2485FFFC 0x00100000
    reps2 compile-words output.cpp 0x00100000 0x24080005 0x24090007 0x01095021

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

The first slice records the frontend command/session lifecycle and final command errors. It does not yet trace every guest instruction or claim that C++ device/runtime events are captured. Runtime tracing will be added as a separate bounded facility so diagnostics cannot turn into per-instruction disk I/O. Error messages can contain local file paths; review diagnostic files before sharing them publicly.

## Agent and IDE developer CLI

The `dev` command namespace is intended for IDE integrations, coding agents, and automated triage. Its inspection commands write exactly one JSON report to stdout; they do not start a new diagnostics session, so inspecting the latest session cannot accidentally select itself.

    reps2 --dev --log-dir .\diagnostics version
    reps2 --log-dir .\diagnostics dev sessions
    reps2 --log-dir .\diagnostics dev sessions --limit 5
    reps2 --log-dir .\diagnostics dev inspect latest
    reps2 --log-dir .\diagnostics dev inspect session-1790000000000-1234-0

`dev sessions` returns recent session identifiers, manifest summaries, event counts, diagnostic counts, and integrity warnings. `dev inspect` returns the raw session manifest as a JSON object, the event stream as a JSON array, warning/error events (excluding the duplicate terminal session summary) in a separate `diagnostics` array, and an `integrity_errors` array for missing or malformed artifacts. The default log root is the same platform-specific directory used by `--dev`; `--log-dir` can select the root and must appear before `dev`.

The report envelope is versioned with `schema_version: 1` and a `kind` discriminator:
- `reps2.developer.session_list`
- `reps2.developer.session_inspection`
- `reps2.developer.error`

The output contract is documented in [docs/schemas/developer-report-v1.schema.json](schemas/developer-report-v1.schema.json). Errors from the `dev` inspection commands use a JSON error envelope on stdout and a non-zero process exit code. Successful inspection commands exit with code 0. Human help is available through `reps2 dev help`.

With `--dev` enabled, regular commands now emit `command.started`, `command.succeeded`, or `command.failed` events under the pipeline category. Each event identifies the top-level command subsystem (for example `iso9660`, `library`, `analysis`, `recompiler`, or `codegen`). Failures include the stable `REPS2_COMMAND_FAILED` code, command name, failure scope, and original error text. This is command-level attribution; it does not yet claim to identify an individual internal function, IR pass, guest instruction, or C++ runtime event. A session interrupted before finalization is returned as incomplete with explicit integrity warnings rather than being marked successful.

Do not scrape human CLI output when an agent needs a session report. Run the relevant command with `--dev`, then call `dev inspect latest` using the same diagnostics root. Review reports before sharing them because error text can include local paths.

## add-game

add-game accepts one raw 2048-byte-sector ISO 9660 image and stores a content-addressed record below the default per-user REPS2 library (`%LOCALAPPDATA%\\REPS2\\Library` on Windows). Set `REPS2_LIBRARY_DIR` or pass `--library <directory>` only to override the default. The generated `game.json` is the launcher-facing metadata contract; `boot.elf`, `recompiled.cpp`, and `ee-main-ram.bin` are stored only when the relevant stage succeeds. The original ISO is referenced by canonical path rather than copied, and duplicate imports are detected by content hash. The command is a managed import pipeline, not a claim that the imported title is playable.

## disc-info and extract-boot-elf

disc-info and extract-boot-elf accept raw 2048-byte-sector ISO 9660 images. The reader uses seeking and bounded directory/file reads, so a multi-gigabyte disc is not loaded into memory as a whole. extract-boot-elf resolves SYSTEM.CNF BOOT2 first and BOOT second, validates the target as an ELF32 little-endian EM_MIPS ET_EXEC, and creates a new output file without overwriting existing paths. Raw 2352-byte BIN/CUE tracks, CHD, UDF, Joliet/Rock Ridge names, multi-volume sets, multi-extent files, interleaved files, HDD PFS targets, and full PS2 optical drive emulation are not supported. The ISO is opened read-only; extraction writes only the separate output ELF.

## analyze-elf

analyze-elf is a read-only, reachability-guided analysis command. It starts from the ELF entry point and executable STT_FUNC symbols, reports basic blocks and static edges, and explicitly lists indirect targets that cannot be resolved. The edge totals include a `syscall` continuation edge for every trap: the address a host service resumes at, which is not an architectural successor because an unserviced trap vectors through COP0. That continuation is queued leniently, since retail images also place data after traps, so an undecodable continuation is reported among the heuristic-only blocks that could not be decoded instead of failing the analysis (see `docs/decisions/0063-syscall-continuation-discovery.md`). It deliberately does not decode zero-filled BSS bytes. With `--discover`, it additionally scans code bytes for `JAL` call targets and canonical `addiu $sp, $sp, -N` stack prologues, and reads file-backed data words for code pointers (callback/vtable and syscall-handler tables). Code bytes and data words are separated by section-declared `SHF_EXECINSTR` ranges when the ELF provides them, falling back to the `PT_LOAD` `PF_X` flag otherwise, so a single writable+executable load segment is still scanned correctly (ADR 0070). Discovered roots are reported as `discovered`, and the output separates accepted roots, rejected candidates, and heuristic-only blocks that could not be decoded; discovery is a candidate heuristic, not a reachability or function-boundary proof. See `docs/decisions/0026-static-function-discovery.md`. Both analysis commands apply explicit safety bounds and accept `--max-blocks`, `--max-instructions` and `--max-roots` overrides. Defaults are 250,000 blocks, 2,000,000 decoded instructions and 500,000 discovered roots; overrides must be positive and stay below CLI ceilings above the physical 32 MiB address space. A block or instruction limit failure names the flag that raises it. See `docs/decisions/0030-analysis-limit-policy.md`.

## profile-elf

profile-elf is a read-only decoder census over every complete, naturally aligned 32-bit word in file-backed executable PT_LOAD segments. It reports supported, trap, and unsupported word counts, supported-opcode totals, and unsupported counts by encoding family and exact word. Because embedded data and unreachable bytes are included, the totals are an address-space census rather than a reachability claim. A separate bounded linear scan from the ELF entry point stops at the first unsupported instruction or trap and reports that address and word.

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

`--native-build` additionally writes `<stem>.native/CMakeLists.txt` and `<stem>.native/probe_main.cpp`: a standalone project that compiles the dispatcher, all parts and a host probe, and registers a CTest. Configure it with `cmake -S <stem>.native -B <build> -DREPS2_RUNTIME_INCLUDE=<repo>/runtime/include`, then build and run CTest; the probe loads `<stem>.ram.bin` (or takes a path and instruction budget as arguments), calls `initialize_ee_state`, and executes with a bounded instruction budget. `--probe-budget N` bakes N into the generated probe and its CTest as the default budget; the default of 0 stops before any guest instruction runs, while a positive budget lets the generated CTest exercise real translated code.

The probe prints one machine-readable trace line: `entry`, `status` (the `Status` enumerator), final `pc`, `budget`, `executed` (guest instructions charged by the block-atomic dispatcher), and the COP0 `cause`/`epc`/`cop0_status`/`badvaddr` registers. Set `REPS2_INSTRUCTION_TRACE=1` to opt into bounded `instruction_trace_pcs` output (up to 16 guest PCs); tracing is disabled by default. The generated CTest uses a `PASS_REGULAR_EXPRESSION` on that line so it fails if the trace is lost.

`--hle-syscalls` enables the runtime EE syscall HLE layer in the generated probe (see `docs/decisions/0062-ee-syscall-hle-dispatch.md`). Without it a guest `syscall` delivers the architectural Level-1 exception exactly as before the layer existed. With it, the service numbered by `$v1` is looked up in the host table: a serviced call resumes after the trap (or at the branch target/fallthrough for a delay slot) and the trace gains `hle`, `syscalls`, `hle_handled`, `hle_pc`, `hle_number`, `hle_v0_before`, `hle_a0`, `hle_a1`, `hle_a2`, `hle_a3`, `hle_sp` and `hle_name`; a service number with no handler stops as `status=unhandled_syscall` with the PC still on the `syscall` and no COP0 context. The layer never returns success for a service it does not implement. The added register fields are host observations captured before dispatch (ADR 0065). The `CreateSema` boundary is reported separately: `sema_calls`, `sema_live`, `sema_param`, `sema_init`, `sema_max`, `sema_attr`, `sema_option`, `sema_id` and `sema_refused` describe the last CreateSema call, where `sema_id=-1` with a `sema_refused` reason means the service returned the documented failure code and allocated nothing (ADR 0066). The trace also carries `syscall_seq_count` and `syscall_seq`: up to 16 recent boundaries as `pc:number:path:a0:a1:a2:a3:v0` records joined by `;`, where `path` is `G` (registered guest handler), `H` (host service) or `U` (unhandled). See `docs/decisions/0071-syscall-sequence-trace.md`. SetSyscall registration is reported through the runtime state and never implies that the registered guest handler executed (ADR 0067).

The generated files contain no absolute paths or timestamps.

`--relocated-code DEST:SOURCE:SIZE` (hexadecimal, repeatable) analyses bytes that a title copies into RAM at run time. `SIZE` bytes are read from the initial EE main-RAM image at virtual address `SOURCE` and analysed as executable guest code at `DEST`; `DEST` must be word-aligned and `SIZE` a nonzero multiple of four. The region becomes an authoritative analysis root and is recorded in the manifest (version 3). The initial RAM image is unchanged, so the guest's own copy remains an observable action; code that is not declared still stops at that address as `unresolved_control_flow`. See `docs/decisions/0074-relocated-code-regions.md`.

### Related decisions

See `docs/decisions/0027-multi-translation-unit-codegen.md`, `docs/decisions/0028-deterministic-build-manifest.md` and `docs/decisions/0029-native-build-driver.md`. Analysis bounds and their overrides are documented in `docs/decisions/0030-analysis-limit-policy.md`. The initial guest-trap classification is documented historically in `docs/decisions/0031-guest-trap-status.md`; the current Level-1 exception/ERET behavior is specified in `docs/decisions/0033-ee-level1-exception-and-eret.md`. The EE syscall HLE dispatch and its `SetupThread`/`SetupHeap`/`CreateSema`/`FlushCache` services are specified in
`docs/decisions/0062-ee-syscall-hle-dispatch.md`,
`docs/decisions/0064-ee-setupheap-hle.md` and `docs/decisions/0066-ee-createsema-hle.md`.

## Development-only commands

compile-words remains a narrow straight-line development command. compile-cfg-fixture is an internal synthetic fixture generator used by CTest. Neither command consumes or distributes game data. `EeState::memory.main_ram` must be supplied by the host as a 32 MiB writable byte span before executing generated code that performs memory operations; the generated module does not allocate or own the console's RAM.
