# REPS2 command-line reference

The CLI is the developer and automation interface. This reference was moved out of the README; the text is unchanged apart from headings and paragraph breaks.

## Commands

    reps2 version
    reps2 disc-info <disc-image.iso>
    reps2 add-game <disc-image.iso> [--library <directory>]
    reps2 extract-boot-elf <disc-image.iso> <output.elf>
    reps2 elf-info <elf-file>
    reps2 analyze-elf <elf-file> [--discover] [--max-blocks N] [--max-instructions N] [--max-roots N]
    reps2 profile-elf <elf-file>
    reps2 recompile-elf <input-elf> <output.cpp> [--discover] [--tu-count N] [--native-build] [--max-blocks N] [--max-instructions N] [--max-roots N]
    reps2 decode <instruction-word-hex> [pc-hex]
    reps2 compile-words <output.cpp> <base-pc-hex> <instruction-word-hex>...

Example:

    reps2 elf-info "D:\List Games\some-owned-game\SLUS_123.45"
    reps2 recompile-elf "D:\List Games\some-owned-game\SLUS_123.45" build\game.cpp
    reps2 decode 0x2485FFFC 0x00100000
    reps2 compile-words output.cpp 0x00100000 0x24080005 0x24090007 0x01095021

## add-game

add-game accepts one raw 2048-byte-sector ISO 9660 image and stores a content-addressed record below the default per-user REPS2 library (`%LOCALAPPDATA%\\REPS2\\Library` on Windows). Set `REPS2_LIBRARY_DIR` or pass `--library <directory>` only to override the default. The generated `game.json` is the launcher-facing metadata contract; `boot.elf`, `recompiled.cpp`, and `ee-main-ram.bin` are stored only when the relevant stage succeeds. The original ISO is referenced by canonical path rather than copied, and duplicate imports are detected by content hash. The command is a managed import pipeline, not a claim that the imported title is playable.

## disc-info and extract-boot-elf

disc-info and extract-boot-elf accept raw 2048-byte-sector ISO 9660 images. The reader uses seeking and bounded directory/file reads, so a multi-gigabyte disc is not loaded into memory as a whole. extract-boot-elf resolves SYSTEM.CNF BOOT2 first and BOOT second, validates the target as an ELF32 little-endian EM_MIPS ET_EXEC, and creates a new output file without overwriting existing paths. Raw 2352-byte BIN/CUE tracks, CHD, UDF, Joliet/Rock Ridge names, multi-volume sets, multi-extent files, interleaved files, HDD PFS targets, and full PS2 optical drive emulation are not supported. The ISO is opened read-only; extraction writes only the separate output ELF.

## analyze-elf

analyze-elf is a read-only, reachability-guided analysis command. It starts from the ELF entry point and executable STT_FUNC symbols, reports basic blocks and static edges, and explicitly lists indirect targets that cannot be resolved. It deliberately does not decode zero-filled BSS bytes. With `--discover`, it additionally scans file-backed executable bytes for `JAL` call targets and canonical `addiu $sp, $sp, -N` stack prologues. Discovered roots are reported as `discovered`, and the output separates accepted roots, rejected candidates, and heuristic-only blocks that could not be decoded; discovery is a candidate heuristic, not a reachability or function-boundary proof. See `docs/decisions/0026-static-function-discovery.md`. Both analysis commands apply explicit safety bounds and accept `--max-blocks`, `--max-instructions` and `--max-roots` overrides. Defaults are 250,000 blocks, 2,000,000 decoded instructions and 500,000 discovered roots; overrides must be positive and stay below CLI ceilings above the physical 32 MiB address space. A block or instruction limit failure names the flag that raises it. See `docs/decisions/0030-analysis-limit-policy.md`.

## profile-elf

profile-elf is a read-only decoder census over every complete, naturally aligned 32-bit word in file-backed executable PT_LOAD segments. It reports supported, trap, and unsupported word counts, supported-opcode totals, and unsupported counts by encoding family and exact word. Because embedded data and unreachable bytes are included, the totals are an address-space census rather than a reachability claim. A separate bounded linear scan from the ELF entry point stops at the first unsupported instruction or trap and reports that address and word.

## recompile-elf

### Inputs and outputs

recompile-elf accepts a static little-endian MIPS ET_EXEC whose PT_LOAD segments all map into the documented default 32 MiB EE main RAM and whose allocated sections have no unapplied relocations. It writes the requested C++ source plus a companion file formed by replacing the output extension with ram.bin (for example, game.cpp and game.ram.bin). The binary is a raw 32 MiB initial RAM image with PT_LOAD bytes copied into physical main-RAM offsets and BSS zero-filled.

### Using the generated code

Generated C++ exports initialize_ee_state(EeState&, std::span<const std::uint8_t>), which validates both buffers, copies the image into caller-owned RAM, clears GPRs, and sets the PC to the ELF entry; it also exports execute_cfg(EeState&, instruction_budget). The caller must load the ram.bin file into a 32 MiB byte vector, attach a writable 32 MiB RAM span to EeState::memory.main_ram, call the generated initializer, then execute.

### Control flow and exceptions

Direct branches, J/JAL, JR/JALR and the supported REGIMM branches are represented with explicit delay slots. Unsupported instructions and out-of-graph control flow return structured statuses. Known `SYSCALL`/`BREAK` instructions now enter the EE Level-1 common exception vector: generated code updates COP0 Cause/EPC/Status (including nested EXL handling and BEV vector selection) and returns `Status::exception_delivered` as a host boundary. `ERET` is lowered as a dynamic control-flow terminator that resumes through EPC or ErrorEPC and clears the corresponding exception-level bit. This does not execute BIOS/HLE syscall services; other exception causes and CFG traps in delay slots remain unsupported. This is a compiler vertical slice, not a complete game recompiler or runnable PS2 module.

### Discovery and translation units

With `--discover`, the same translation starts from the discovered candidate roots as well; stripping retail binaries yields thousands of candidates and a correspondingly large CFG. Every block is emitted as its own `block_0xXXXXXXXX` function dispatched through a sorted table. `--tu-count N` writes the dispatcher to the requested output and N block parts beside it as `<stem>.partNNNN.cpp` (stale parts from an earlier run are removed); the default of 1 keeps a single self-contained file. On Black/USA, `--discover --tu-count 8` produces a 15.84 MiB dispatcher plus eight 17.89-23.87 MiB parts that compile and link with MSVC `/O2 /W4 /permissive-` in about 149 s sequentially with zero warnings.

### Build manifest

recompile-elf also writes `<stem>.manifest.json` beside the translation: it records the manifest schema version, tool version and host target, the input path/size/SHA-256, analysis limits and discovery totals, program counts, the exact translation-unit and part layout, the RAM artifact size, and `playable: false`. Rendering is deterministic, so an identical invocation produces a byte-identical manifest.

### Native build project

`--native-build` additionally writes `<stem>.native/CMakeLists.txt` and `<stem>.native/probe_main.cpp`: a standalone project that compiles the dispatcher, all parts and a host probe, and registers a CTest. Configure it with `cmake -S <stem>.native -B <build> -DREPS2_RUNTIME_INCLUDE=<repo>/runtime/include`, then build and run CTest; the probe loads `<stem>.ram.bin` (or takes a path and instruction budget as arguments), calls `initialize_ee_state`, and reports one `execute_cfg` result. The generated files contain no absolute paths or timestamps.

### Related decisions

See `docs/decisions/0027-multi-translation-unit-codegen.md`, `docs/decisions/0028-deterministic-build-manifest.md` and `docs/decisions/0029-native-build-driver.md`. Analysis bounds and their overrides are documented in `docs/decisions/0030-analysis-limit-policy.md`. The initial guest-trap classification is documented historically in `docs/decisions/0031-guest-trap-status.md`; the current Level-1 exception/ERET behavior is specified in `docs/decisions/0033-ee-level1-exception-and-eret.md`.

## Development-only commands

compile-words remains a narrow straight-line development command. compile-cfg-fixture is an internal synthetic fixture generator used by CTest. Neither command consumes or distributes game data. `EeState::memory.main_ram` must be supplied by the host as a 32 MiB writable byte span before executing generated code that performs memory operations; the generated module does not allocate or own the console's RAM.
