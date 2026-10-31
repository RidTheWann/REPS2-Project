# Contributing to REPS2

## Development workflow

1. Create a focused branch from main.
2. Keep changes small enough to review and test.
3. Run Cargo formatting, Clippy, and unit tests for Rust changes.
4. Run CMake build and CTest for integrated changes.
5. Explain observable behavior, source references, unsupported cases, and test evidence in the pull request.

## Rust frontend expectations

- Use safe Rust by default; unsafe code requires a dedicated design review.
- Propagate input and semantic errors with Result; avoid panic-based control flow.
- Represent unsupported guest operations explicitly.
- Preserve source guest PC and provenance through analysis and IR.
- Keep decoder functions deterministic and side-effect free.
- Keep resource limits on all untrusted sizes, counts, and allocations.

## C++20 runtime expectations

- Use fixed-width guest types, RAII, and clear ownership.
- Do not use host signed-overflow behavior or undefined behavior to emulate guest semantics.
- Keep guest addresses separate from host pointers.
- Make unsupported services visible; never fake success.

## Test data and legal boundaries

- Use synthetic instruction streams, ELF files, memory maps, and device responses.
- Do not commit game images, BIOS files, proprietary SDK material, extracted retail code, or copyrighted assets.
- Local tests against lawfully obtained game data must keep that data outside the repository and CI.
- Do not vendor third-party emulator or recompiler source code.

## Pull-request evidence

Include the commands and results used for testing, references supporting semantic changes, and an explicit list of remaining limitations.
