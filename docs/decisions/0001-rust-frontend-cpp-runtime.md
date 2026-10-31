# ADR 0001: Rust frontend and C++20 runtime

- Status: Accepted
- Date: 2026-10-31
- Scope: REPS2 toolchain language architecture

## Context

The original repository foundation used C++20 for ELF parsing and a small base-MIPS decoder. These components are correctness-critical and process untrusted binary data. The proposed Rust adoption maps Rust to parsing, decoding, analysis, typed IR, and code generation, while keeping a native C++20 runtime for platform and subsystem integration.

## Decision

Adopt a hybrid architecture:
- Rust is the canonical frontend implementation: disc/ELF readers, decoder, CFG/function discovery, IR, IR verifier, optimizers, AOT source generation, CLI, diagnostics, and frontend tests.
- Generated C++20 source plus a machine-readable manifest is the principal frontend/runtime handoff.
- C++20 remains the runtime implementation language for EE services and platform-facing subsystems.
- CMake orchestrates builds; Cargo owns Rust compilation, tests, dependency lock, and linting.

Do not introduce a Rust static library + FFI bridge merely to call a standalone compiler frontend. The source/manifest boundary is sufficient and keeps Rust IR ownership private. Reconsider CXX or a narrow C ABI only if future measurements prove that in-process calls are needed. If used, calls must be coarse-grained; no per-instruction FFI calls.

## Rationale

Rust offers bounds-checked slices, explicit error propagation, algebraic data types, and exhaustive matching over closed enums. These are particularly useful for untrusted ELF parsing, decoder classification, typed IR, and verifier passes. Ownership and Send/Sync constraints make parallel analysis easier to reason about, but they do not automatically make all concurrency logic correct; unsafe code and foreign interfaces still require auditing.

AOT source generation is naturally part of the Rust frontend. The resulting C++ translation unit is then compiled and linked with runtime code. This avoids moving complex IR graphs across FFI and avoids a language boundary on each guest operation.

The runtime stays in C++20 to preserve the current native-runtime direction and integrate with host graphics/audio/platform APIs. This is a project decision, not a claim that Rust is inherently too slow or unsuitable for emulation code.

## Corrections to the initial adoption proposal

- unimplemented!() panics at runtime; it is not a compile-time diagnostic. Unsupported guest instructions should be represented by a typed error/unsupported variant and propagated with Result.
- compile_error!() stops compilation when the macro is expanded; it is appropriate for invalid build configurations, not per-input instruction decoding.
- Exhaustive matching helps ensure all known IR variants are handled, but a decoder still needs an explicit unknown/unsupported representation for arbitrary 32-bit instruction words.
- Rust's ownership model prevents many data races only when safe aliasing and Send/Sync contracts are respected. It is not a blanket correctness guarantee for parallel CFG algorithms.
- The runtime's low Rust fit is not a performance limitation. C++ is retained for architectural continuity and host integration, not because FFI overhead would make Rust runtime code inherently slow.

## Build integration

The frontend is a standalone Rust executable invoked by CMake/Cargo. Corrosion supports importing Rust binaries and libraries as CMake targets, but a static library is not required for the chosen process boundary. Avoid fetching an extra build integration dependency until it provides concrete value.

## Consequences

Positive:
- Stronger parser and IR safety without rewriting the future runtime.
- No high-frequency FFI overhead or cross-language ownership of IR trees.
- Dependency-free Rust parser/decoder foundation is easy to audit.
- Rust unit tests can be run independently of the C++ runtime.

Trade-offs:
- Developers need stable Rust and Cargo in addition to the future C++ toolchain.
- CMake must invoke Cargo deterministically and share its build directory.
- Generated-code/runtime integration and ABI versioning still need careful design.
- The current Rust parser supports only a bounded ELF32 little-endian subset; PS2 ABI and R5900 semantics remain incomplete.
