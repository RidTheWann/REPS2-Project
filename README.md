# REPS2

**REPS2** is an independent PlayStation 2 static recompilation toolchain targeting native Windows x64 execution.

The architecture uses a Rust frontend for executable inspection, instruction decoding, analysis, typed IR, optimization, and AOT source generation. Generated C++ is compiled by the platform host compiler and linked against an independently implemented C++20 PS2 runtime as runtime subsystems are added.

> **Status: experimental foundation (v0.2.0).** The repository currently provides a defensive ELF32 little-endian metadata parser, initial symbol/REL relocation handling, PT_LOAD materialization, a deliberately partial base-MIPS decoder, a CLI, synthetic tests, and Windows/Linux CI. It does not yet build a complete AOT translation unit, run a game, or implement the PS2 hardware runtime.

## Current frontend capabilities

- Parse ELF32 little-endian headers, program headers, section headers, section names, symbol tables, and REL/RELA entries.
- Validate file-backed ranges, table bounds, cross-table references, entry sizes, address overflow, and parser resource limits.
- Materialize PT_LOAD segments with zero-filled memory-only tails; reject overlapping guest virtual ranges.
- Decode an explicitly scoped base-MIPS integer subset with delay-slot metadata and direct branch/jump targets.
- Report unsupported instruction encodings explicitly rather than replacing them with NOPs.
- Build the Rust frontend through Cargo, with CMake as the top-level build orchestrator.
- Run synthetic tests without game ISOs, BIOS files, proprietary SDK content, or copyrighted fixtures.

## Requirements

- Stable Rust toolchain (edition 2024).
- CMake 3.25 or newer.
- Git (for source checkout).

The C++20 runtime is a separate future build component. C++ compiler requirements will be added to the build when runtime implementation lands.

## Build

From the repository root:

    cmake -S . -B build -DBUILD_TESTING=ON
    cmake --build build --config Release --parallel
    ctest --test-dir build -C Release --output-on-failure

The frontend executable is produced at:

- Windows: build/cargo-target/release/reps2.exe
- Linux: build/cargo-target/release/reps2

Alternatively, work directly with Cargo:

    cargo test --manifest-path rust/Cargo.toml --workspace --locked
    cargo build --manifest-path rust/Cargo.toml --release --locked

## CLI

    reps2 version
    reps2 elf-info <elf-file>
    reps2 decode <instruction-word-hex> [pc-hex]

Example:

    reps2 elf-info "D:\List Games\some-owned-game\SLUS_123.45"
    reps2 decode 0x2485FFFC 0x00100000

The decode command is a single-word diagnostic, not a complete disassembler or executable recompiler.

## Architecture

The intended pipeline is:

1. Disc/filesystem inspection and executable extraction.
2. ELF loading, symbols, relocations, and guest memory map.
3. R5900 decoding, function discovery, and control-flow graph construction.
4. Typed, host-independent IR with verifiable control-flow and side-effect invariants.
5. IR optimization and deterministic C++20 source generation.
6. Host compilation/linking of generated code and the C++20 runtime.
7. Modular runtime services for EE, VU, GS, IOP, DMA, audio, input, and storage.
8. Differential testing and game-stage regression.

See docs/ARCHITECTURE.md, docs/ROADMAP.md, and docs/decisions/0001-rust-frontend-cpp-runtime.md.

## Engineering and legal boundaries

- REPS2 is an independent implementation. External projects may be studied for architecture and observable behavior, but their source code is not copied or incorporated.
- Synthetic fixtures only. Do not commit game ISOs, BIOS dumps, proprietary SDKs, copyrighted game assets, or user-specific binaries.
- Game data used for local validation must be lawfully obtained and remain outside this repository and CI.
- Unsupported instructions and hardware behaviors must fail explicitly; silent approximations are not correctness claims.
- Every behavior-changing implementation requires tests and, where possible, a differential test case.

## License

REPS2 is licensed under GNU General Public License v3.0 or later. See LICENSE.
