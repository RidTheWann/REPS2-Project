# REPS2 roadmap

The roadmap is capability-based. Dates are deliberately not used as acceptance criteria; each milestone has executable evidence.

## M0 — Reproducible foundation (in progress)

- [x] C++20 / CMake project structure.
- [x] ELF32 little-endian header and table validation.
- [x] Initial base-MIPS decoder subset.
- [x] Explicit delay-slot and direct-target metadata.
- [x] Synthetic tests and Windows/Linux CI workflow.
- [ ] Confirm first GitHub Actions run on the repository.

**Exit gate:** clean build and passing tests on MSVC and GCC; malformed ELF input is rejected deterministically.

## M1 — ELF and executable intelligence

- [ ] PT_LOAD mapping model and zero-filled memory tails.
- [ ] ELF section names, symbol tables, string tables, and relocation parsing.
- [ ] PS2-specific ELF metadata/ABI notes and unsupported-variant diagnostics.
- [ ] Address-space model with explicit guest/host separation.
- [ ] A synthetic ELF fixture containing code, data, symbols, and relocations.

**Exit gate:** inspect a legally owned PS2 ELF locally and produce a stable machine-readable manifest without storing the ELF in Git.

## M2 — R5900 semantics and code discovery

- [ ] Expand decoder coverage from the official R5900 instruction set.
- [ ] MMI0–MMI3 and 64-bit integer operations.
- [ ] COP0 and FPU instruction models.
- [ ] Branch-likely/exception/unaligned-access cases as applicable to the R5900.
- [ ] Function/CFG discovery with explicit unresolved indirect edges.
- [ ] Independent instruction semantic tests and differential corpus.

**Exit gate:** supported instructions have reference-backed semantic tests; unknown instruction coverage is quantified.

## M3 — IR and generated-code vertical slice

- [ ] Typed guest IR and basic-block representation.
- [ ] Delay-slot-safe CFG construction.
- [ ] Verifier for IR invariants and control-flow termination.
- [ ] C++20 lowering for a documented instruction subset.
- [ ] Compile and execute generated code against a synthetic EE-state harness.

**Exit gate:** a synthetic ELF code region can be decoded, lowered, compiled, and produce the expected guest state transitions.

## M4 — First real executable slice

- [ ] Build an owner-supplied ELF manifest and code map locally.
- [ ] Resolve entry points and internal calls for one selected title.
- [ ] Implement the minimum required EE runtime services.
- [ ] Produce a game-specific native module.
- [ ] Record the exact boot stage and every unresolved service.

**Exit gate:** native execution reaches a repeatable, evidenced stage in a real title. This is not yet a playable-game claim.

## M5 — Runtime subsystem expansion

- [ ] EE memory, exceptions, interrupts, timers, and DMA behavior.
- [ ] VU0 macro-mode support and VU1 microprogram pipeline.
- [ ] IOP/IRX execution and kernel/HLE service surface.
- [ ] GS submission and rendering path.
- [ ] Audio, controller, optical media, and memory-card services.

**Exit gate:** subsystem suites pass and a game-specific stage advances without unexplained fallbacks.

## M6 — Compatibility, performance, and release engineering

- [ ] Multi-game regression catalog keyed by title/serial/version, with no copyrighted binaries.
- [ ] Differential tests for all implemented hardware-visible behavior.
- [ ] Native performance profiling and code-generation optimization.
- [ ] Installer/package, crash diagnostics, settings, and user documentation.
- [ ] Release compatibility claims backed by reproducible test reports.

**Exit gate:** a release candidate has defined supported titles, known limitations, clean-room provenance, and reproducible CI.

## Prioritization policy

Prioritize work that unblocks the next vertical slice. Do not spend weeks reverse-engineering a subsystem that the current target does not exercise. Prefer public manuals and independently authored specifications; investigate only ambiguities that materially affect implementation or test results.
