# REPS2

**REPS2** is an independent research project to build a native Windows x64 PlayStation 2 static recompilation toolchain.

The long-term goal is to translate supported Emotion Engine (EE) machine code ahead of time into C++ that can be compiled for the host, then execute it against an independently implemented PS2 runtime. The project is developed from public technical specifications, controlled experiments, synthetic tests, and differential behavior checks. It does not reuse another emulator or recompiler's implementation code.

> **Status: experimental foundation (v0.1.0).** This repository currently contains a defensive ELF32 little-endian metadata parser, a deliberately partial base-MIPS instruction decoder, a small CLI, synthetic unit tests, and Windows/Linux CI. It does not yet recompile a complete ELF, boot a game, or emulate PS2 hardware.

## Current capabilities

- Inspect ELF32 little-endian headers, program headers, loadable segments, and section headers.
- Validate file-backed ranges and reject malformed table/segment bounds before exposing metadata.
- Decode an explicitly scoped subset of base MIPS integer instructions.
- Identify branch/jump delay slots and calculate direct control-flow targets for supported instructions.
- Build with C++20 and CMake on Windows/MSVC and Linux/GCC.
- Run synthetic tests without distributing or requiring copyrighted game images, BIOS files, or proprietary SDK content.

## Build

Requirements:

- CMake 3.25 or newer.
- A C++20 compiler (Visual Studio 2022 17.4+, Visual Studio 2026, GCC 12+, or Clang 15+).

PowerShell (Visual Studio Developer PowerShell/Command Prompt for MSVC):

    cmake -S . -B build -DREPS2_BUILD_TESTS=ON
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure

A generator can be selected explicitly, for example -G Ninja when Ninja is installed.

## CLI

    reps2 version
    reps2 elf-info <elf-file>
    reps2 decode <instruction-word-hex> [pc-hex]

Examples:

    reps2 elf-info "D:\List Games\some-owned-game\SLUS_123.45"
    reps2 decode 0x2485FFFC 0x00100000

The decode command accepts one 32-bit instruction word in hexadecimal. It is a decoder diagnostic, not a full disassembler or ELF recompilation command.

## Architecture direction

The intended pipeline is:

1. Disc/ELF inspection and executable extraction.
2. ELF loading, segment mapping, symbols, relocations, and entry-point discovery.
3. R5900 decoding and control-flow/function discovery.
4. A typed, host-independent intermediate representation (IR).
5. AOT C++ lowering with explicit PS2 integer, floating-point, delay-slot, and memory semantics.
6. Native host compilation and linking.
7. A modular runtime for EE services and the PS2 subsystems required by each target.
8. Differential testing and compatibility regression against documented behavior and trusted external references.

See docs/ARCHITECTURE.md, docs/ROADMAP.md, and docs/RESEARCH.md.

## Engineering and legal boundaries

- REPS2 is an independent implementation. External projects may be studied for architecture and observable behavior, but their source code is not copied or incorporated.
- Test fixtures are synthetic and generated in tests. Do not commit game ISOs, BIOS dumps, proprietary SDKs, copyrighted game assets, or user-specific binaries.
- Game data, when used by a developer for local validation, must be lawfully obtained and remain outside this repository.
- Unsupported instructions and hardware behaviors must fail explicitly; silent approximations are not acceptable as correctness claims.
- Every behavior-changing implementation should include tests and, where possible, a differential test case.

## License

REPS2 is licensed under GNU General Public License v3.0 or later. See LICENSE.
