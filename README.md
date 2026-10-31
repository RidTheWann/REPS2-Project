# REPS2

**REPS2** is an independent PlayStation 2 static recompilation toolchain targeting native Windows x64 execution.

The frontend is written in Rust: executable inspection, instruction decoding, analysis, typed IR, verification, and AOT C++ source generation. Generated C++ is compiled by the host compiler and linked against the independently implemented C++20 runtime.

> **Status: experimental foundation (v0.3.0).** REPS2 now has a bounded ELF32 little-endian parser, initial symbols and REL/RELA metadata, PT_LOAD materialization, a deliberately partial base-MIPS decoder, a typed straight-line IR, and a deterministic C++20 emitter for a documented scalar subset. CMake builds a synthetic program through Rust-to-C++ lowering, compiles it with the host compiler, and executes it in CI on Windows and Linux. This does not yet recompile a PS2 ELF, build a complete game module, boot a title, or implement the PS2 hardware runtime.

## Current capabilities

- Parse ELF32 little-endian headers, program headers, section headers and section names, symbol tables, and REL/RELA entries.
- Validate file-backed ranges, table bounds, cross-table references, entry sizes, address overflow, and parser resource limits.
- Materialize PT_LOAD segments with zero-filled memory-only tails; reject overlapping guest virtual ranges.
- Decode an explicitly scoped base-MIPS integer subset with delay-slot metadata and direct branch/jump targets.
- Lift a supported straight-line instruction subset into typed Rust IR with guest-PC provenance and verification.
- Emit deterministic C++20 for supported scalar operations: NOP, ADD/ADDI/ADDU/ADDIU, SUB/SUBU, AND/OR/XOR, immediate logical operations, LUI, and word shifts.
- Keep a 128-bit EE GPR state shape in the runtime ABI; scalar instructions update only the low 64-bit lane and preserve the high lane.
- Compile and execute a generated synthetic translation unit as part of CMake/CTest on Windows/MSVC and Linux/GCC.
- Report unsupported control-flow, memory, and hardware behavior explicitly; do not silently replace it with NOPs.

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

The CMake build also generates build/generated/synthetic_program.cpp, compiles it with the host C++20 compiler, and runs reps2_codegen_smoke.

For Rust-only work:

    cargo fmt --manifest-path rust/Cargo.toml --all -- --check
    cargo clippy --manifest-path rust/Cargo.toml --workspace --all-targets -- -D warnings
    cargo test --manifest-path rust/Cargo.toml --workspace --locked

## CLI

    reps2 version
    reps2 elf-info <elf-file>
    reps2 decode <instruction-word-hex> [pc-hex]
    reps2 compile-words <output.cpp> <base-pc-hex> <instruction-word-hex>...

Example:

    reps2 elf-info "D:\List Games\some-owned-game\SLUS_123.45"
    reps2 decode 0x2485FFFC 0x00100000
    reps2 compile-words output.cpp 0x00100000 0x24080005 0x24090007 0x01095021

compile-words is a narrow linear-block development command. It accepts raw instruction words, lifts only the supported scalar subset, and writes a C++ translation unit. It is not an ELF-to-game recompiler. Branches and delay-slot control flow, memory operations, function discovery, full R5900 semantics, and runtime service integration remain future work.

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

## Engineering and legal boundaries

- REPS2 is an independent implementation. External projects may be studied for architecture and observable behavior, but their source code is not copied or incorporated.
- Synthetic fixtures only. Do not commit game ISOs, BIOS dumps, proprietary SDKs, copyrighted game assets, or user-specific binaries.
- Game data used for local validation must be lawfully obtained and remain outside this repository and CI.
- Unsupported instructions and hardware behaviors must fail explicitly; silent approximations are not correctness claims.
- Every behavior-changing implementation requires tests and, where possible, a differential test case.

## License

REPS2 is licensed under GNU General Public License v3.0 or later. See LICENSE.
