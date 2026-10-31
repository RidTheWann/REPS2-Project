# REPS2

**REPS2** is an independent PlayStation 2 static recompilation toolchain targeting native Windows x64 execution.

The frontend is written in Rust: executable inspection, instruction decoding, analysis, typed IR, verification, and AOT C++ source generation. Generated C++ is compiled by the host compiler and linked against the independently implemented C++20 runtime.

> **Status: experimental AOT foundation (v0.7.0).** REPS2 now has a bounded read-only ISO 9660 reader for raw 2048-byte-sector images with SYSTEM.CNF BOOT2/BOOT executable discovery, a bounded ELF32 little-endian parser, initial symbols and REL/RELA metadata, a deliberately partial base-MIPS decoder, a typed scalar IR, a reachability-guided CFG, a CFG-aware control IR, deterministic C++20 lowering for a documented scalar/control-flow subset, byte/halfword/word/doubleword/quadword EE main-RAM loads and stores, including the R5900 left/right partial word and doubleword merge family, plus PT_LOAD-to-main-RAM image construction and a generated ELF-entry initializer. CMake builds synthetic scalar, control-flow, memory and initialization programs, compiles the generated C++, and executes them in CI on Windows and Linux. The ELF-to-C++ command now emits a C++ translation and a companion 32 MiB initial RAM image for eligible static ELFs; full R5900 coverage, relocations, TLB/exception semantics, MMIO devices, system services, native module linking, and game compatibility are not complete.

## Current capabilities

- Inspect raw 2048-byte-sector ISO 9660 images without loading the entire disc; validate the descriptor sequence and single-volume PVD, list root entries, resolve nested paths, and parse SYSTEM.CNF with BOOT2 preferred over BOOT.
- Extract the SYSTEM.CNF boot target only after validating it as a MIPS ET_EXEC ELF; use create-new output semantics so an existing file or source image is never overwritten.
- Parse ELF32 little-endian headers, program headers, section headers and section names, symbol tables, and REL/RELA entries.
- Validate file-backed ranges, table bounds, cross-table references, entry sizes, address overflow, and parser resource limits.
- Materialize PT_LOAD segments with zero-filled memory-only tails; reject overlapping guest virtual ranges.
- Decode an explicitly scoped base-MIPS integer subset, including BEQ/BNE/BLEZ/BGTZ and REGIMM BLTZ/BGEZ branch families.
- Discover entry points and STT_FUNC roots, then build a reachability-guided CFG for the currently supported control-transfer subset.
- Preserve control instructions and delay slots as explicit terminators; record direct taken/fallthrough/call/return-continuation edges and unresolved indirect targets.
- Read executable instructions only from file-backed PT_LOAD bytes; zero-filled BSS is never misinterpreted as code.
- Lift discovered blocks into a CFG-aware typed IR with explicit fallthroughs, branch conditions, direct/indirect jumps, returns, and delay-slot operations.
- Lower the supported CFG subset to a C++20 PC dispatcher; preserve branch/link/delay-slot ordering and stop explicitly at unsupported operations or unmapped control flow.
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

The CMake build generates build/generated/synthetic_program.cpp and build/generated/control_flow.cpp, compiles both with the host C++20 compiler, and runs reps2_codegen_smoke plus reps2_cfg_codegen_smoke.

For Rust-only work:

    cargo fmt --manifest-path rust/Cargo.toml --all -- --check
    cargo clippy --manifest-path rust/Cargo.toml --workspace --all-targets -- -D warnings
    cargo test --manifest-path rust/Cargo.toml --workspace --locked

## CLI

    reps2 version
    reps2 disc-info <disc-image.iso>
    reps2 extract-boot-elf <disc-image.iso> <output.elf>
    reps2 elf-info <elf-file>
    reps2 analyze-elf <elf-file>
    reps2 recompile-elf <input-elf> <output.cpp>
    reps2 decode <instruction-word-hex> [pc-hex]
    reps2 compile-words <output.cpp> <base-pc-hex> <instruction-word-hex>...

Example:

    reps2 elf-info "D:\List Games\some-owned-game\SLUS_123.45"
    reps2 recompile-elf "D:\List Games\some-owned-game\SLUS_123.45" build\game.cpp
    reps2 decode 0x2485FFFC 0x00100000
    reps2 compile-words output.cpp 0x00100000 0x24080005 0x24090007 0x01095021

disc-info and extract-boot-elf accept raw 2048-byte-sector ISO 9660 images. The reader uses seeking and bounded directory/file reads, so a multi-gigabyte disc is not loaded into memory as a whole. extract-boot-elf resolves SYSTEM.CNF BOOT2 first and BOOT second, validates the target as an ELF32 little-endian EM_MIPS ET_EXEC, and creates a new output file without overwriting existing paths. Raw 2352-byte BIN/CUE tracks, CHD, UDF, Joliet/Rock Ridge names, multi-volume sets, multi-extent files, interleaved files, HDD PFS targets, and full PS2 optical drive emulation are not supported. The ISO is opened read-only; extraction writes only the separate output ELF.

analyze-elf is a read-only, reachability-guided analysis command. It starts from the ELF entry point and executable STT_FUNC symbols, reports basic blocks and static edges, and explicitly lists indirect targets that cannot be resolved. It deliberately does not decode zero-filled BSS bytes.

recompile-elf accepts a static little-endian MIPS ET_EXEC whose PT_LOAD segments all map into the documented default 32 MiB EE main RAM and whose allocated sections have no unapplied relocations. It writes the requested C++ source plus a companion file formed by replacing the output extension with ram.bin (for example, game.cpp and game.ram.bin). The binary is a raw 32 MiB initial RAM image with PT_LOAD bytes copied into physical main-RAM offsets and BSS zero-filled. Generated C++ exports initialize_ee_state(EeState&, std::span<const std::uint8_t>), which validates both buffers, copies the image into caller-owned RAM, clears GPRs, and sets the PC to the ELF entry; it also exports execute_cfg(EeState&, instruction_budget). The caller must load the ram.bin file into a 32 MiB byte vector, attach a writable 32 MiB RAM span to EeState::memory.main_ram, call the generated initializer, then execute. Direct branches, J/JAL, JR/JALR and the supported REGIMM branches are represented with explicit delay slots. Unsupported instructions and out-of-graph control flow return structured statuses. This is a compiler vertical slice, not a complete game recompiler or runnable PS2 module.

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
- Branch-and-link variants write the PC+8 return address regardless of whether the branch predicate is true. The link is written before the delay-slot instruction executes.
- JALR captures its dynamic destination before writing the link register, preserving correct behavior when source and destination registers alias.
- The CFG AOT dispatcher executes the branch/jump delay slot before committing the selected next PC. Branch-likely/annul semantics are not included in the supported subset.
- EE load/store effective addresses use the low 32-bit base register plus a sign-extended 16-bit offset, wrapping as a 32-bit address. LB/LH/LW sign-extend into GPR[63:0]; LBU/LHU/LWU zero-extend into GPR[63:0]; stores write the low 8/16/32 bits and preserve the upper GPR lane.
- Main-RAM reads/writes are little-endian and support the documented 32 MiB aliases. Halfword and word accesses require natural alignment; a faulting load does not modify its destination register.
- MMIO, VU memory, IOP RAM, BIOS, scratchpad, reserved ranges, TLB remapping, cache effects and bus/exception delivery are not emulated by this memory slice. Recognized device/reserved regions return `unsupported_memory_access`; out-of-range or absent backing RAM returns `unmapped_memory`.

## Engineering and legal boundaries

- REPS2 is an independent implementation. External projects may be studied for architecture and observable behavior, but their source code is not copied or incorporated.
- Synthetic fixtures only. Do not commit game ISOs, BIOS dumps, proprietary SDKs, copyrighted game assets, or user-specific binaries.
- Game data used for local validation must be lawfully obtained and remain outside this repository and CI.
- Unsupported instructions and hardware behaviors must fail explicitly; silent approximations are not correctness claims.
- Every behavior-changing implementation requires tests and, where possible, a differential test case.

## License

REPS2 is licensed under GNU General Public License v3.0 or later. See LICENSE.
