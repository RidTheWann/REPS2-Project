# REPS2 architecture

## Design objective

REPS2 is a game-independent static recompilation toolchain. It must not require complete reverse engineering of all PS2 hardware before the first real executable can be translated. Development proceeds through vertical slices: choose an executable, discover the exact code and runtime behaviors it needs, implement those behaviors behind stable interfaces, then expand coverage.

## Language boundaries

### Rust frontend

Rust owns:
- Disc and ELF parsing.
- Program/section mapping, symbols, relocations, and executable metadata.
- EE instruction decoding and control-flow discovery.
- Function/basic-block construction and typed IR.
- IR verification and optimization.
- Deterministic C++20 source and build-manifest generation.
- CLI orchestration, diagnostics, and frontend tests.

This is the correctness-critical input and transformation pipeline. Rust's checked indexing, explicit Result propagation, enums, and exhaustive pattern matching are used to make invalid input and unsupported operations explicit.

### C++20 runtime

C++20 owns the native runtime and platform-facing subsystem implementations:
- Guest memory and EE runtime services.
- VU0/VU1 execution interfaces.
- GS submission and rendering integration.
- IOP, DMA, SPU2/audio, controller, optical media, and memory-card services.
- Generated-code helper ABI and host integration.

The runtime remains modular. A target game needs only the runtime profile required to advance its current vertical slice, but every fallback is observable and logged.

### Boundary

The primary boundary is generated source plus a machine-readable manifest, not per-instruction FFI calls. The Rust frontend emits C++ and metadata; the host C++ compiler compiles that output and links it with the C++ runtime. This keeps complex Rust IR ownership inside Rust and avoids passing IR graphs or vectors across a language ABI.

A narrow C ABI or CXX bridge may be introduced later only when a measured use case requires in-process calls. Such a bridge must use coarse-grained operations and stable POD/opaque handles; it must not cross the boundary for every guest instruction.

CMake orchestrates the build, Cargo builds/tests the Rust frontend, and the host C++ toolchain is added to the build as soon as runtime source is introduced. Corrosion is available for importing Rust executables or libraries into CMake; it is not required merely to invoke a standalone Rust frontend.

## Pipeline

    Owned disc image (local only)
            |
            v
    Disc / filesystem inspection
            |
            v
    ELF loader ----> sections / symbols / relocations / load map
            |
            v
    R5900 decoder ----> function discovery / CFG / delay slots
            |
            v
    Typed IR ----> verifier ----> optimization
            |
            v
    C++20 AOT backend ----> generated C++ + build manifest
            |
            v
    Host C++ compiler / linker
            |
            v
    Native game module + REPS2 C++20 runtime
            |
            v
    Runtime profiles: EE / VU / GS / IOP / DMA / audio / input / storage

## Module invariants

### ELF and disc readers

Validate every offset, size, table count, cross-table index, and arithmetic operation before access or allocation. Distinguish file size from memory size and zero-fill BSS tails. Extended ELF numbering and uncommon PS2-specific ABI cases must remain explicit until supported and tested.

### EE decoder and analysis

Decoding is pure and deterministic. Every input word produces a classified instruction or an explicit unsupported result. Static analysis must not assume every byte in an executable segment is code. Indirect control-flow targets remain unresolved until evidence is available.

### IR

IR nodes retain guest PC and side-effect information. Delay-slot behavior, guest register width, exception behavior, and architectural ordering must be represented explicitly. An IR verifier rejects operations with no defined semantics or backend lowering.

### AOT backend

Generated code uses explicit-width types and helper functions for guest arithmetic, memory, and architectural behavior. It must not rely on host signed-overflow behavior, pointer punning, or undefined behavior. Code generation is deterministic for the same input and options.

### Runtime

Runtime interfaces are versioned and narrow. Every unimplemented system service returns a structured unsupported result or controlled guest-visible failure; it must not return fake success.

### Validation

Synthetic tests, instruction semantics, generated-code tests, differential checks, and game-stage regression are separate layers. Parsing a real ELF or compiling a generated translation unit is not evidence that a game boots or is playable.

## Current implementation

- Rust ELF32 little-endian parser with section names, symbol tables, and REL/RELA metadata.
- Bounded PT_LOAD materialization with zero-filled tails and overlap detection.
- Initial base-MIPS decoder subset with delay-slot metadata.
- CLI diagnostics for ELF metadata and instruction words.
- Synthetic tests and Windows/Linux CI.

No complete R5900 semantic engine, CFG, IR, C++ AOT emitter, or PS2 runtime is claimed yet.
