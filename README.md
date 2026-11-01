# REPS2

**REPS2** is an independent PlayStation 2 static recompilation toolchain targeting native Windows x64 execution.

The frontend is written in Rust: executable inspection, instruction decoding, analysis, typed IR, verification, and AOT C++ source generation. Generated C++ is compiled by the host compiler and linked against the independently implemented C++20 runtime.

> **Status: experimental AOT foundation (v0.7.0).** REPS2 now has a bounded read-only ISO 9660 reader for raw 2048-byte-sector images with SYSTEM.CNF BOOT2/BOOT executable discovery, a bounded ELF32 little-endian parser, initial symbols and REL/RELA metadata, a deliberately partial R5900 decoder with a deterministic executable-byte coverage profiler and bounded entry-point scan, a typed scalar IR with the first lane-saturating MMI operation (PADDUW), the EE single-precision FPU's data-movement set (MFC1/MTC1/CFC1/CTC1/LWC1/SWC1), its S/W-class arithmetic on an integer-only, non-IEEE numeric model with the separate ACC register and FCR31 flag routing, and the four COP1 compare predicates with the BC1F/BC1T/BC1FL/BC1TL branch family driven by FCR31's condition bit, a reachability-guided CFG, a CFG-aware control IR, deterministic C++20 lowering for a documented scalar/control-flow subset, byte/halfword/word/doubleword/quadword EE main-RAM loads and stores, including the R5900 left/right partial word and doubleword merge family, plus PT_LOAD-to-main-RAM image construction and a generated ELF-entry initializer. Generated control-flow C++ now emits one function per basic block with sorted-table dispatch and can be split across a dispatcher plus multiple translation units. CMake builds synthetic scalar, R5900 HI/LO/multiply, control-flow, memory and initialization programs, compiles the generated C++, and executes them in CI on Windows and Linux. The ELF-to-C++ command now emits a C++ translation, a companion 32 MiB initial RAM image, and a deterministic machine-readable build manifest for eligible static ELFs; full R5900 coverage, relocations, TLB mapping, interrupts and exception types beyond the basic SYSCALL/BREAK Level-1 path, MMIO devices, system services, native module linking, and game compatibility are not complete.

## Current capabilities

- Add one owned PS2 ISO directly to a managed multi-game library with `reps2 add-game`; identify the disc by streaming SHA-256, resolve SYSTEM.CNF BOOT2/BOOT, extract and validate the boot ELF internally, and write a versioned per-game JSON manifest plus any currently available analysis/code-generation artifacts. The original ISO remains at its source path and is not copied. Imported records are explicitly marked non-playable until the native PS2 runtime and title-stage validation exist.

- Inspect raw 2048-byte-sector ISO 9660 images without loading the entire disc; validate the descriptor sequence and single-volume PVD, list root entries, resolve nested paths, and parse SYSTEM.CNF with BOOT2 preferred over BOOT.
- Extract the SYSTEM.CNF boot target only after validating it as a MIPS ET_EXEC ELF; use create-new output semantics so an existing file or source image is never overwritten.
- Parse ELF32 little-endian headers, program headers, section headers and section names, symbol tables, and REL/RELA entries.
- Validate file-backed ranges, table bounds, cross-table references, entry sizes, address overflow, and parser resource limits.
- Materialize PT_LOAD segments with zero-filled memory-only tails; reject overlapping guest virtual ranges.
- Decode an explicitly scoped base-MIPS integer subset, including BEQ/BNE/BLEZ/BGTZ and REGIMM BLTZ/BGEZ branch families.
- Expose conservative per-instruction GPR/HI-LO dependencies, memory/control-flow effects, and possible synchronous exceptions; unsupported encodings remain explicitly opaque.
- Decode, lift and AOT-lower base EE MTHI/MTLO/MFHI/MFLO plus R5900 three-operand MULT/MULTU; preserve HI/LO, the optional GPR destination, and both EE GPR lanes.
- Decode, lift and AOT-lower 64-bit NOR, signed/unsigned SLT/SLTU, and SLTI/SLTIU with signed immediate extension and low-lane-only result writes.
- Decode, lift and AOT-lower trapping 64-bit DADD, DSUB and DADDI with defined overflow checks; preserve destination atomicity, GPR[0] trap behavior and upper-lane state.
- Discover entry points and STT_FUNC roots, then build a reachability-guided CFG for the currently supported control-transfer subset.
- Recover function-entry candidates in stripped executables with opt-in `--discover`: `JAL` targets backed by executable file bytes and canonical `addiu $sp, $sp, -N` prologues. Candidates are labeled and are not reachability claims; heuristic-only blocks that cannot decode are skipped and reported, while authoritative roots still fail hard.
- Preserve control instructions and delay slots as explicit terminators; record direct taken/fallthrough/call/return-continuation edges and unresolved indirect targets.
- Read executable instructions only from file-backed PT_LOAD bytes; zero-filled BSS is never misinterpreted as code.
- Lift discovered blocks into a CFG-aware typed IR with explicit fallthroughs, branch conditions, direct/indirect jumps, returns, and delay-slot operations.
- Lower the supported CFG subset to a C++20 PC dispatcher; preserve branch/link/delay-slot ordering and stop explicitly at unsupported operations or unmapped control flow.
- Emit each basic block as an independent `block_0xXXXXXXXX` C++ function and dispatch through a sorted table with `std::lower_bound`. `--tu-count N` splits a program into a dispatcher plus `N` independently compilable block parts, and the single-unit default remains available.
- Write a deterministic `<stem>.manifest.json` beside every `recompile-elf` translation: the input path, size and SHA-256, host target, analysis limits and discovery totals, program counts, translation-unit layout, RAM artifact, and an explicit `playable: false` claim. Repeated identical invocations are byte-identical because no timestamps are recorded.
- Generate a deterministic standalone `<stem>.native/` CMake project and host probe with `--native-build`. The project compiles the dispatcher, every part and the probe against an explicit `REPS2_RUNTIME_INCLUDE`, then registers a CTest that loads the RAM image (when present) and performs one bounded `execute_cfg` call. A CTest integration configures, compiles and runs the generated project end to end.
- Bound analysis with explicit, validated limits and let the developer CLI override them. Defaults are 250,000 basic blocks, 2,000,000 decoded instructions and 500,000 discovered roots; `--max-blocks`, `--max-instructions` and `--max-roots` raise them up to ceilings above the 32 MiB address space. Limit failures name the flag that raises the bound instead of failing silently.
- Bound generated execution by a guest-instruction budget, admitting each basic block atomically and preserving its starting PC when the budget is insufficient.
- Decode and lift LB/LBU/LH/LHU/LW/LWU, LD, SB/SH/SW and SD into typed memory IR with width-correct lane updates, natural alignment checks and signed 16-bit offsets.
- Decode and lower LWL/LWR/SWL/SWR and LDL/LDR/SDL/SDR through checked aligned main-RAM accesses and byte-lane merges; preserve unaffected EE GPR lanes and fail explicitly on invalid backing spans.
- Provide caller-owned 32 MiB little-endian EE main RAM with the documented direct, uncached, accelerated, KSEG0 and KSEG1 aliases.
- Perform checked byte/halfword/word/doubleword accesses without unaligned pointer casts; doubleword transfers require 8-byte alignment; report address errors, unmapped ranges, and unimplemented device/MMIO accesses as distinct statuses.
- Materialize eligible ET_EXEC PT_LOAD segments into a 32 MiB initial EE RAM image, including zero-filled BSS and supported RAM aliases; reject physical alias overlap and out-of-RAM segments.
- Emit a generated initialize_ee_state entry-point wrapper that installs the companion RAM image into caller-owned EE RAM and sets the ELF entry PC.
- Reject ET_DYN/relocatable ELFs and relocations targeting allocated sections until a verified relocation engine is implemented.
- Lift a supported straight-line instruction subset into typed Rust IR with guest-PC provenance and verification.
- Emit deterministic C++20 for supported scalar operations: NOP, ADD/ADDI/ADDU/ADDIU, SUB/SUBU, AND/OR/XOR, immediate logical operations, LUI, and word shifts.
- Keep a 128-bit EE GPR state shape in the runtime ABI; scalar instructions update only the low 64-bit lane and preserve the high lane.
- Compile and execute a generated synthetic translation unit as part of CMake/CTest on Windows/MSVC and Linux/GCC.
- Report unsupported control-flow, MMIO, TLB, exception, and hardware behavior explicitly; do not silently replace it with NOPs or backing RAM.

## Requirements

- Stable Rust toolchain (edition 2024).
- CMake 3.25 or newer.
- A C++20 compiler (Visual Studio 2022 17.4+, GCC 12+, or Clang 15+).
- Git.

## Build and test

From the repository root:

    cmake -S . -B build -DBUILD_TESTING=ON
    cmake --build build --config Release --parallel
    ctest --test-dir build -C Release --output-on-failure

The frontend executable is produced at:

- Windows: build/cargo-target/release/reps2.exe
- Linux: build/cargo-target/release/reps2

The CMake build generates eight translation units under `build/generated/` (`synthetic_program.cpp`, `compare_nor_program.cpp`, `signed_doubleword_program.cpp`, `hilo_program.cpp`, `sa_program.cpp`, `cop1_program.cpp`, `cop1_arith_program.cpp`, `control_flow.cpp`), compiles each with the host C++20 compiler into a native smoke executable, and runs them together with the Rust test target through CTest. A ninth fixture emits the same control-flow graph as a dispatcher (`control_flow_split.cpp`) plus four block parts (`control_flow_split.part0001.cpp` through `control_flow_split.part0004.cpp`) and links them into `reps2_cfg_split_codegen_smoke`, pinning the multi-translation-unit path.

For Rust-only work:

    cargo fmt --manifest-path rust/Cargo.toml --all -- --check
    cargo clippy --manifest-path rust/Cargo.toml --workspace --all-targets -- -D warnings
    cargo test --manifest-path rust/Cargo.toml --workspace --locked

## CLI

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

add-game accepts one raw 2048-byte-sector ISO 9660 image and stores a content-addressed record below the default per-user REPS2 library (`%LOCALAPPDATA%\\REPS2\\Library` on Windows). Set `REPS2_LIBRARY_DIR` or pass `--library <directory>` only to override the default. The generated `game.json` is the launcher-facing metadata contract; `boot.elf`, `recompiled.cpp`, and `ee-main-ram.bin` are stored only when the relevant stage succeeds. The original ISO is referenced by canonical path rather than copied, and duplicate imports are detected by content hash. The command is a managed import pipeline, not a claim that the imported title is playable.

disc-info and extract-boot-elf accept raw 2048-byte-sector ISO 9660 images. The reader uses seeking and bounded directory/file reads, so a multi-gigabyte disc is not loaded into memory as a whole. extract-boot-elf resolves SYSTEM.CNF BOOT2 first and BOOT second, validates the target as an ELF32 little-endian EM_MIPS ET_EXEC, and creates a new output file without overwriting existing paths. Raw 2352-byte BIN/CUE tracks, CHD, UDF, Joliet/Rock Ridge names, multi-volume sets, multi-extent files, interleaved files, HDD PFS targets, and full PS2 optical drive emulation are not supported. The ISO is opened read-only; extraction writes only the separate output ELF.

analyze-elf is a read-only, reachability-guided analysis command. It starts from the ELF entry point and executable STT_FUNC symbols, reports basic blocks and static edges, and explicitly lists indirect targets that cannot be resolved. It deliberately does not decode zero-filled BSS bytes. With `--discover`, it additionally scans file-backed executable bytes for `JAL` call targets and canonical `addiu $sp, $sp, -N` stack prologues. Discovered roots are reported as `discovered`, and the output separates accepted roots, rejected candidates, and heuristic-only blocks that could not be decoded; discovery is a candidate heuristic, not a reachability or function-boundary proof. See `docs/decisions/0026-static-function-discovery.md`. Both analysis commands apply explicit safety bounds and accept `--max-blocks`, `--max-instructions` and `--max-roots` overrides. Defaults are 250,000 blocks, 2,000,000 decoded instructions and 500,000 discovered roots; overrides must be positive and stay below CLI ceilings above the physical 32 MiB address space. A block or instruction limit failure names the flag that raises it. See `docs/decisions/0030-analysis-limit-policy.md`.

profile-elf is a read-only decoder census over every complete, naturally aligned 32-bit word in file-backed executable PT_LOAD segments. It reports supported, trap, and unsupported word counts, supported-opcode totals, and unsupported counts by encoding family and exact word. Because embedded data and unreachable bytes are included, the totals are an address-space census rather than a reachability claim. A separate bounded linear scan from the ELF entry point stops at the first unsupported instruction or trap and reports that address and word.

recompile-elf accepts a static little-endian MIPS ET_EXEC whose PT_LOAD segments all map into the documented default 32 MiB EE main RAM and whose allocated sections have no unapplied relocations. It writes the requested C++ source plus a companion file formed by replacing the output extension with ram.bin (for example, game.cpp and game.ram.bin). The binary is a raw 32 MiB initial RAM image with PT_LOAD bytes copied into physical main-RAM offsets and BSS zero-filled. Generated C++ exports initialize_ee_state(EeState&, std::span<const std::uint8_t>), which validates both buffers, copies the image into caller-owned RAM, clears GPRs, and sets the PC to the ELF entry; it also exports execute_cfg(EeState&, instruction_budget). The caller must load the ram.bin file into a 32 MiB byte vector, attach a writable 32 MiB RAM span to EeState::memory.main_ram, call the generated initializer, then execute. Direct branches, J/JAL, JR/JALR and the supported REGIMM branches are represented with explicit delay slots. Unsupported instructions and out-of-graph control flow return structured statuses. Known `SYSCALL`/`BREAK` instructions now enter the EE Level-1 common exception vector: generated code updates COP0 Cause/EPC/Status (including nested EXL handling and BEV vector selection) and returns `Status::exception_delivered` as a host boundary. `ERET` is lowered as a dynamic control-flow terminator that resumes through EPC or ErrorEPC and clears the corresponding exception-level bit. This does not execute BIOS/HLE syscall services; other exception causes and CFG traps in delay slots remain unsupported. This is a compiler vertical slice, not a complete game recompiler or runnable PS2 module. With `--discover`, the same translation starts from the discovered candidate roots as well; stripping retail binaries yields thousands of candidates and a correspondingly large CFG. Every block is emitted as its own `block_0xXXXXXXXX` function dispatched through a sorted table. `--tu-count N` writes the dispatcher to the requested output and N block parts beside it as `<stem>.partNNNN.cpp` (stale parts from an earlier run are removed); the default of 1 keeps a single self-contained file. On Black/USA, `--discover --tu-count 8` produces a 15.84 MiB dispatcher plus eight 17.89-23.87 MiB parts that compile and link with MSVC `/O2 /W4 /permissive-` in about 149 s sequentially with zero warnings. recompile-elf also writes `<stem>.manifest.json` beside the translation: it records the manifest schema version, tool version and host target, the input path/size/SHA-256, analysis limits and discovery totals, program counts, the exact translation-unit and part layout, the RAM artifact size, and `playable: false`. Rendering is deterministic, so an identical invocation produces a byte-identical manifest. `--native-build` additionally writes `<stem>.native/CMakeLists.txt` and `<stem>.native/probe_main.cpp`: a standalone project that compiles the dispatcher, all parts and a host probe, and registers a CTest. Configure it with `cmake -S <stem>.native -B <build> -DREPS2_RUNTIME_INCLUDE=<repo>/runtime/include`, then build and run CTest; the probe loads `<stem>.ram.bin` (or takes a path and instruction budget as arguments), calls `initialize_ee_state`, and reports one `execute_cfg` result. The generated files contain no absolute paths or timestamps. See `docs/decisions/0027-multi-translation-unit-codegen.md`, `docs/decisions/0028-deterministic-build-manifest.md` and `docs/decisions/0029-native-build-driver.md`. Analysis bounds and their overrides are documented in `docs/decisions/0030-analysis-limit-policy.md`. Guest-trap statuses are documented in `docs/decisions/0031-guest-trap-status.md`.

compile-words remains a narrow straight-line development command. compile-cfg-fixture is an internal synthetic fixture generator used by CTest. Neither command consumes or distributes game data. `EeState::memory.main_ram` must be supplied by the host as a 32 MiB writable byte span before executing generated code that performs memory operations; the generated module does not allocate or own the console's RAM.


## Architecture

1. Disc/filesystem inspection and executable extraction.
2. ELF loading, symbols, relocations, and guest memory mapping.
3. R5900 decoding, function discovery, and delay-slot-aware CFG construction.
4. Typed, host-independent IR and verifier.
5. Deterministic C++20 AOT source generation plus a machine-readable build manifest.
6. Host compilation/linking of generated code and the C++20 runtime.
7. Modular runtime services for EE, VU, GS, IOP, DMA, audio, input, and storage.
8. Differential testing and game-stage regression.

See docs/ARCHITECTURE.md, docs/ROADMAP.md, and docs/decisions/.

## Semantic boundaries

- Word operations validate the R5900 sign-extended word-value precondition, use 32-bit modulo/overflow behavior as specified by the operation, then sign-extend the word result into GPR[63:0].
- Logical 64-bit operations affect GPR[63:0] only; the upper 64-bit GPR lane is preserved.
- GPR[0] is hard-wired to zero.
- Status::integer_overflow reports an overflow outcome for ADD/ADDI; the full EE exception vector/CP0 path is not implemented yet.
- Status::undefined_word_operand is an explicit REPS2 diagnostic guard for an operand outside the documented word-value restriction; it is not presented as a distinct hardware exception.
- ORI follows the MIPS zero-extended-immediate definition and the observed R5900 reference behavior; the discrepancy in one publicly mirrored Sony manual text is recorded in docs/decisions/0002-scalar-ir-and-cpp-aot.md.
- Conditional branch predicates read the low 64-bit EE GPR value; signed branches use a bit-preserving signed interpretation rather than host-dependent casts.
- Branch-and-link variants, including BLTZALL/BGEZALL, write the PC+8 return address regardless of whether the branch predicate is true. The link is written before the delay-slot instruction executes.
- JALR captures its dynamic destination before writing the link register, preserving correct behavior when source and destination registers alias.
- The CFG AOT dispatcher evaluates the branch predicate and writes any link address before the delay slot. For branch-likely instructions, the delay slot executes only on the taken path and is annulled on fallthrough; the selected next PC is committed after the delay-slot side effects.
- EE load/store effective addresses use the low 32-bit base register plus a sign-extended 16-bit offset, wrapping as a 32-bit address. LB/LH/LW sign-extend into GPR[63:0]; LBU/LHU/LWU zero-extend into GPR[63:0]; stores write the low 8/16/32 bits and preserve the upper GPR lane.
- Main-RAM reads/writes are little-endian and support the documented 32 MiB aliases. Halfword and word accesses require natural alignment; a faulting load does not modify its destination register.
- MMIO, VU memory, IOP RAM, BIOS, scratchpad, reserved ranges, TLB remapping, cache effects and bus/exception delivery are not emulated by this memory slice. Recognized device/reserved regions return `unsupported_memory_access`; out-of-range or absent backing RAM returns `unmapped_memory`.
- SYSCALL and BREAK are delivered as Level-1 EE exceptions with Cause.ExcCode 8/9, EPC/BD state, EXL and the normal or BEV bootstrap common vector. Nested Level-1 exceptions preserve the first EPC/BD frame. ERET resumes through EPC or ErrorEPC; an ambiguous EXL+ERL context is rejected rather than guessed. Delay-slot traps remain excluded from CFG lowering, and no BIOS/HLE syscall service is delivered.

## Engineering and legal boundaries

- REPS2 is an independent implementation. External projects may be studied for architecture and observable behavior, but their source code is not copied or incorporated.
- Synthetic fixtures only. Do not commit game ISOs, BIOS dumps, proprietary SDKs, copyrighted game assets, or user-specific binaries.
- Game data used for local validation must be lawfully obtained and remain outside this repository and CI.
- Unsupported instructions and hardware behaviors must fail explicitly; silent approximations are not correctness claims.
- Every behavior-changing implementation requires tests and, where possible, a differential test case.

## License

REPS2 is licensed under GNU General Public License v3.0 or later. See LICENSE.
