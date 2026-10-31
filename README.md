# REPS2

**REPS2** is an independent PlayStation 2 static recompilation toolchain targeting native Windows x64 execution.

The frontend is written in Rust: executable inspection, instruction decoding, analysis, typed IR, verification, and AOT C++ source generation. Generated C++ is compiled by the host compiler and linked against the independently implemented C++20 runtime.

> **Status: experimental AOT foundation (v0.5.0).** REPS2 now has a bounded ELF32 little-endian parser, initial symbols and REL/RELA metadata, PT_LOAD materialization, a deliberately partial base-MIPS decoder, a typed scalar IR, a reachability-guided CFG, a CFG-aware control IR, and deterministic C++20 lowering for a documented scalar/control-flow subset. CMake builds synthetic scalar and branch/call programs, compiles the generated C++, and executes both in CI on Windows and Linux. The ELF-to-C++ command is usable for owned ELF files, but full R5900 coverage, memory semantics, system services, native module linking, and game compatibility are not complete.

## Current capabilities

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

The CMake build generates build/generated/synthetic_program.cpp and build/generated/control_flow.cpp, compiles both with the host C++20 compiler, and runs reps2_codegen_smoke plus reps2_cfg_codegen_smoke.

For Rust-only work:

    cargo fmt --manifest-path rust/Cargo.toml --all -- --check
    cargo clippy --manifest-path rust/Cargo.toml --workspace --all-targets -- -D warnings
    cargo test --manifest-path rust/Cargo.toml --workspace --locked

## CLI

    reps2 version
    reps2 elf-info <elf-file>
    reps2 analyze-elf <elf-file>
    reps2 recompile-elf <input-elf> <output.cpp>
    reps2 decode <instruction-word-hex> [pc-hex]
    reps2 compile-words <output.cpp> <base-pc-hex> <instruction-word-hex>...

Example:

    reps2 elf-info "D:\List Games\some-owned-game\SLUS_123.45"
    reps2 decode 0x2485FFFC 0x00100000
    reps2 compile-words output.cpp 0x00100000 0x24080005 0x24090007 0x01095021

analyze-elf is a read-only, reachability-guided analysis command. It starts from the ELF entry point and executable STT_FUNC symbols, reports basic blocks and static edges, and explicitly lists indirect targets that cannot be resolved. It deliberately does not decode zero-filled BSS bytes.

recompile-elf performs ELF-to-CFG-IR-to-C++ lowering for the currently supported scalar and control-flow subset. The output exports reps2::generated::execute_cfg(EeState&, instruction_budget); the caller supplies the initial guest PC and EE state. Direct branches, J/JAL, JR/JALR, and the supported REGIMM branches are represented with explicit delay slots. Unsupported instructions and control transfers outside the translated graph return a structured status instead of being guessed. This is a compiler vertical slice, not a complete game recompiler or runnable PS2 module.

compile-words remains a narrow straight-line development command. compile-cfg-fixture is an internal synthetic fixture generator used by CTest. Neither command consumes or distributes game data.


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

## Engineering and legal boundaries

- REPS2 is an independent implementation. External projects may be studied for architecture and observable behavior, but their source code is not copied or incorporated.
- Synthetic fixtures only. Do not commit game ISOs, BIOS dumps, proprietary SDKs, copyrighted game assets, or user-specific binaries.
- Game data used for local validation must be lawfully obtained and remain outside this repository and CI.
- Unsupported instructions and hardware behaviors must fail explicitly; silent approximations are not correctness claims.
- Every behavior-changing implementation requires tests and, where possible, a differential test case.

## License

REPS2 is licensed under GNU General Public License v3.0 or later. See LICENSE.
