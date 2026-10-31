# Contributing to REPS2

## Development workflow

1. Create a focused branch from main.
2. Keep each change narrowly scoped and add tests for observable behavior.
3. Build with CMake and run CTest before opening a pull request.
4. Explain the guest-visible behavior, source references, known limitations, and test evidence in the PR.
5. Do not merge changes that silently approximate unsupported instructions or hardware behavior.

## Code expectations

- C++20, explicit-width integer types, RAII, and clear ownership.
- No undefined behavior to emulate guest overflow, alignment, or aliasing.
- Validate untrusted file sizes, offsets, counts, and arithmetic before reading or allocating.
- Keep decoders pure and deterministic where possible.
- Preserve guest PC and source provenance through analysis and IR.
- Every unsupported operation must remain visible in diagnostics.

## Test data and legal boundaries

- Use synthetic instruction streams, ELF images, memory maps, and device responses.
- Do not commit game images, BIOS files, proprietary SDK content, extracted retail code, or copyrighted assets.
- Local tests against lawfully obtained game data must keep that data outside the repository and CI.
- Never add a third-party emulator/recompiler source tree as a dependency or vendor its implementation code.

## Pull-request evidence

A good PR includes build output or CI links, test cases, the specification or observation supporting changed semantics, and a concise statement of what is still unsupported.
