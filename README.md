# REPS2

**REPS2** is an independent PlayStation 2 static recompilation toolchain targeting native Windows x64 execution.

The frontend is written in Rust: executable inspection, instruction decoding, analysis, typed IR, verification, and AOT C++ source generation. Generated C++ is compiled by the host compiler and linked against the independently implemented C++20 runtime.

> **Status: experimental AOT foundation (v0.7.0).** REPS2 can import an owned ISO, extract and validate its boot ELF, analyze it, and generate deterministic C++20 for a documented R5900 subset: scalar and 64-bit integer operations, HI/LO and multiply, main-RAM loads and stores, control flow with delay slots, part of the EE FPU, and Level-1 SYSCALL/BREAK/overflow/address-error exceptions. Execution is verified on synthetic programs in CI on Windows and Linux. No title is boot-tested yet and every imported game is recorded as non-playable. Full R5900 coverage, relocations, TLB mapping, interrupts, MMIO devices, system services, and game compatibility are not complete. See [docs/CAPABILITIES.md](docs/CAPABILITIES.md) for the detailed list and [docs/ROADMAP.md](docs/ROADMAP.md) for the milestones.

## Current capabilities

The full list (ISO and ELF handling, decoder and IR coverage, code generation, memory model, exceptions, build manifest and native build driver) is in [docs/CAPABILITIES.md](docs/CAPABILITIES.md).

## Requirements

- Rust 1.88 or newer (stable toolchain, edition 2024; the code uses let-chains).
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

The CMake build generates nine scalar/control translation units under `build/generated/` (`synthetic_program.cpp`, `compare_nor_program.cpp`, `signed_doubleword_program.cpp`, `word_overflow_program.cpp`, `hilo_program.cpp`, `sa_program.cpp`, `cop1_program.cpp`, `cop1_arith_program.cpp`, `control_flow.cpp`), compiles each with the host C++20 compiler into a native smoke executable, and runs them together with the Rust test target through CTest. A separate split-CFG fixture emits the same control-flow graph as a dispatcher (`control_flow_split.cpp`) plus four block parts (`control_flow_split.part0001.cpp` through `control_flow_split.part0004.cpp`) and links them into `reps2_cfg_split_codegen_smoke`, pinning the multi-translation-unit path.

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

Per-command behavior, analysis limits, produced artifacts and the generated-code contract are documented in [docs/CLI.md](docs/CLI.md).

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
- Signed `ADD`, `ADDI`, `SUB`, `DADD`, `DADDI` and `DSUB` overflow is delivered as EE Level-1 `ExceptionCode::Overflow` (Cause.ExcCode 12), with no destination write on fault. The common exception vector is `0x80000180` (or `0xBFC00380` when BEV is set); `0x80000200` is reserved for the distinct interrupt vector. Arithmetic overflow in a branch delay slot records EPC/BD correctly; not-taken branch-likely slots remain annulled.
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
