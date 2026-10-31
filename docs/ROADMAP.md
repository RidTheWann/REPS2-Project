# REPS2 roadmap

Milestones are capability gates, not calendar promises. Reverse engineering and implementation proceed together; research is prioritized by the next executable vertical slice.

## M0 — Reproducible hybrid foundation

- [x] Clean repository and GPL-3.0-or-later licensing.
- [x] Rust workspace and dependency lock.
- [x] Defensive ELF32 little-endian metadata parsing.
- [x] Initial symbol/REL relocation parsing.
- [x] PT_LOAD materialization with zero-filled tails and overlap checks.
- [x] Explicitly partial base-MIPS decoder and delay-slot metadata.
- [x] CMake/Cargo build orchestration and Windows/Linux CI.
- [ ] Confirm Rust/Windows/Linux CI for the Rust migration.

**Exit gate:** Rust frontend builds on Windows and Linux; malformed ELF inputs are rejected deterministically.

## M1 — Executable intelligence

- [ ] Complete ELF string/symbol/relocation validation, PS2 ABI metadata, and supported ELF variant diagnostics.
- [ ] PT_LOAD address-space model, permissions, and unmapped-range behavior.
- [ ] PS2 optical filesystem inspection and executable extraction, read-only.
- [ ] Real local ELF inventory manifest with no binaries committed.
- [ ] Entry point, function symbol, and executable-region discovery.

**Exit gate:** inspect a lawfully owned PS2 ELF and produce a stable JSON manifest plus a map of loadable code/data ranges.

## M2 — R5900 decoder and analysis

- [ ] Cover the documented R5900 integer, 64-bit, MMI0–MMI3, COP0, and FPU instruction families.
- [ ] Encode instruction operand/side-effect/exception metadata.
- [ ] Delay-slot-aware basic block and CFG construction.
- [ ] Direct call/branch recovery and unresolved indirect-edge tracking.
- [ ] Function discovery from symbols, entry points, and call graph.
- [ ] Differential instruction corpus against documented behavior and an external oracle.

**Exit gate:** instruction coverage report, semantic test suite, and CFG output for real ELF code regions.

## M3 — IR and generated-code vertical slice

- [ ] Typed Rust IR and architectural-state model.
- [ ] IR verifier for type, control-flow, and side-effect invariants.
- [ ] Delay-slot-safe code lowering.
- [ ] Deterministic C++20 source and build-manifest generation.
- [ ] Synthetic generated C++ compiled and executed against a test runtime.

**Exit gate:** a synthetic ELF code region can be decoded, lowered, compiled, and produce the expected guest-state transitions.

## M4 — First real executable slice

- [ ] Analyze a developer-supplied local ELF, kept outside Git.
- [ ] Discover and translate a real code region and direct callees.
- [ ] Implement only the EE runtime services required by that path.
- [ ] Produce a game-specific native module with unresolved services listed.
- [ ] Record an exact repeatable boot/entry stage.

**Exit gate:** native execution reaches an evidenced stage in one real title. This is not yet a playable-game claim.

## M5 — Runtime subsystem expansion

- [ ] EE memory, exceptions, interrupts, timers, and DMA.
- [ ] VU0 macro-mode and VU1 microprogram compilation/execution.
- [ ] IOP/IRX execution and kernel service surface.
- [ ] GS submission, rendering, frame presentation, and synchronization.
- [ ] Audio, controller, optical media, and memory-card services.

**Exit gate:** a target stage advances without unexplained fallbacks.

## M6 — Compatibility, performance, and release engineering

- [ ] Multi-game compatibility catalog keyed by title/serial/revision, with no copyrighted binaries.
- [ ] Differential tests for implemented hardware-visible behavior.
- [ ] Native profiling and code-generation optimization.
- [ ] Packaging, crash diagnostics, settings, and user documentation.
- [ ] Releases with explicit compatibility evidence and reproducible builds.

**Exit gate:** release candidate has a defined tested-title list, known limitations, provenance, and reproducible CI.
