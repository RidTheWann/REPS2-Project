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

**Exit gate:** Rust frontend builds on Windows and Linux; malformed ELF inputs are rejected deterministically.

## M1 — Executable intelligence

- [ ] Complete ELF string/symbol/relocation validation, PS2 ABI metadata, and supported ELF variant diagnostics.
- [ ] PT_LOAD address-space model, permissions, and unmapped-range behavior.
- [ ] PS2 optical filesystem inspection and executable extraction, read-only.
- [ ] Real local ELF inventory manifest with no binaries committed.
- [x] Discover ELF entry points and executable STT_FUNC symbol roots.
- [x] Reachability-guided CFG for the implemented branch/jump subset; keep indirect targets unresolved.
- [x] Distinguish file-backed executable bytes from zero-filled PT_LOAD tails during instruction reads.

**Exit gate:** inspect a lawfully owned PS2 ELF and produce a stable JSON manifest plus a map of loadable code/data ranges.

## M2 — R5900 decoder and analysis

- [ ] Cover the documented R5900 integer, 64-bit, MMI0–MMI3, COP0, and FPU instruction families.
- [ ] Encode instruction operand/side-effect/exception metadata.
- [x] Delay-slot-aware basic block and CFG construction for a bounded control-transfer subset.
- [x] Direct branch/call recovery and unresolved indirect-edge tracking.
- [x] Entry/function root discovery from ELF entry and STT_FUNC symbols (indirect function discovery remains unresolved).
- [ ] Full documented R5900 branch, branch-likely, and control-transfer coverage.
- [ ] Differential instruction corpus against documented behavior and an external oracle.

**Exit gate:** full instruction coverage report, semantic test suite, and CFG output for real ELF code regions. The current bounded CFG implementation is an intermediate milestone, not completion of M2.

## M3 — IR and generated-code vertical slice

- [x] Typed Rust IR for a documented straight-line scalar subset.
- [x] IR verifier for register, PC alignment/sequencing, and word-shift invariants.
- [x] Deterministic C++20 source generation for the implemented IR operations.
- [x] Synthetic generated C++ compiled and executed against the minimal EE state ABI on Windows and Linux CI.
- [ ] Delay-slot-safe CFG and branch IR.
- [ ] Memory-operation IR with alignment, address-space and exception outcomes.
- [ ] Build manifest with input hash, compiler options, and generated function metadata.
- [ ] ELF-to-IR-to-C++ compilation path for a real executable code region.

**Exit gate:** a synthetic instruction block can be lifted, verified, lowered, compiled, and produce the expected guest-state transitions.

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
