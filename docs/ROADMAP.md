# REPS2 roadmap

Milestones are capability gates, not calendar promises. Reverse engineering and implementation proceed together; research is prioritized by the next executable vertical slice. A checked box records verified implementation evidence, not a claim that a whole milestone or product is complete.

The product definition and architectural principles are maintained in [PRODUCT_VISION.md](PRODUCT_VISION.md). This roadmap translates that vision into ordered engineering and product acceptance gates.

## Product north star

**REPS2 is a Windows PC platform for native recompilation and execution of multiple PlayStation 2 games.** The intended consumer workflow is deliberately simple:

1. The user selects one lawfully owned PS2 ISO in REPS2.
2. REPS2 identifies the disc and boot executable, performs internal extraction and analysis, translates the supported guest code, builds a native game module, and registers the title.
3. The title appears in a persistent game library with an honest compatibility/build status.
4. The user selects **Play**; REPS2 starts the title module with the shared PS2 runtime and the correct per-game configuration.
5. Later launches reuse only verified build artifacts whose complete cache identity matches the input and toolchain.

The ISO is the user's only required game input. The normal user must not need to extract BOOT.ELF, prepare folders, understand ELF/IR/compiler internals, run developer commands, or separately source a proprietary BIOS. The original ISO remains user-owned and is not silently modified or copied as part of the current library design.

### Non-negotiable platform properties

- **Multi-game by architecture:** disc handling, ELF analysis, R5900 decoding, typed IR, code generation, build orchestration, runtime ABI, and shared PS2 services are title-agnostic. A single game is a vertical-slice test target, never the architecture.
- **One shared runtime:** title-specific metadata, compatibility findings, assets, settings, saves, and narrowly isolated workarounds belong to that title's record. They must not become a separate hand-written recompiler or silently fork shared instruction/device semantics.
- **Native recompilation is the execution strategy:** a parser, interpreter-only path, generated C++ file, or successful host compile is not evidence of a native-recompiled game running.
- **No proprietary firmware distribution or dependency in the intended UX:** implement required boot/kernel-facing behavior through independently developed HLE where technically and legally appropriate. Do not distribute Sony firmware, game executables, assets, or proprietary SDK material.
- **Evidence before compatibility claims:** synthetic fixtures prove isolated semantics only. Real-title claims must be bound to an exact disc digest/revision, REPS2 revision, runtime configuration, host target, and repeatable test evidence.
- **Windows is the first-class desktop target:** internal developer CLI and CI can remain cross-platform, while the consumer platform is designed and validated around Windows PC.

### Compatibility states and product gates

The library must distinguish these states; no state implies a later one:

- **Imported:** ISO identity, supported disc structure, boot target, and ELF validation are recorded.
- **Analyzed:** load map, code/data ranges, function roots, CFG coverage, and unresolved analysis items are recorded.
- **Translated:** supported guest regions have verified IR/native-code output; unsupported instructions, relocations, and control-flow edges remain visible.
- **Native-built:** the generated title module has been compiled and linked against a specific shared runtime ABI.
- **Boot-tested:** a named real title reaches a precisely documented, repeatable guest-visible boot stage through that runtime.
- **Playable:** a documented representative gameplay acceptance plan passes, covering the relevant rendering, controls, audio, session stability, and save/load behavior. Any untested subsystem or known limitation remains disclosed.

**Minimum multi-game proof:** at least two genuinely different PS2 titles (distinct title identities/revisions, not merely two copies or modified images of one title) must traverse the same shared analysis/code-generation/runtime path and reach independently repeatable native boot-tested stages. Per-title service profiles are allowed; per-title forks of the core recompiler/runtime semantics are not.

**Stable multi-game release target:** at least two titles must be documented as Playable under explicit test plans. The compatibility catalog must also publish titles that are only imported, analyzed, translated, native-built, boot-tested, or unsupported. This is a target gate, not a claim about current capability.

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
- [x] Default EE main-RAM physical mapping for supported direct, uncached, accelerated and KSEG PT_LOAD aliases.
- [x] PT_LOAD-to-32 MiB initial RAM image construction with BSS zeroing and physical alias overlap checks.
- [ ] Full address-space permissions and COP0/TLB mapping.
- [x] Read-only ISO 9660 inspection and SYSTEM.CNF BOOT2/BOOT ELF extraction for raw 2048-byte-sector images; reject unsupported containers and directory features explicitly.
- [x] Content-addressed, per-game ISO import manifest with internally extracted boot ELF and bounded analysis/code-generation status; original ISO remains external and no game binaries are committed.
- [x] Discover ELF entry points and executable STT_FUNC symbol roots.
- [x] Reachability-guided CFG for the implemented branch/jump subset; keep indirect targets unresolved.
- [x] Distinguish file-backed executable bytes from zero-filled PT_LOAD tails during instruction reads.

**Exit gate:** inspect a lawfully owned PS2 ELF and produce a stable JSON manifest plus a map of loadable code/data ranges.

## M2 — R5900 decoder and analysis

- [ ] Cover the documented R5900 integer, 64-bit, MMI0–MMI3, COP0, and FPU instruction families.
- [x] Decode, lift, and AOT-lower the non-trapping 64-bit scalar subset (DADDU, DSUBU, DADDIU, DSLL/DSRL/DSRA and their 32-shift variants); test modulo arithmetic, sign-extended immediates, shift boundaries, and upper GPR-lane preservation.
- [x] Decode, lift, and AOT-lower register-variable SLLV/SRLV/SRAV and DSLLV/DSRLV/DSRAV; test 5-bit/6-bit shift-count masking, word sign extension, arithmetic sign fill, and GPR upper-lane preservation through compiled native code.
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
- [x] CFG-aware typed IR for supported branch/jump terminators, with explicit scalar delay-slot operations.
- [x] Typed LB/LBU/LH/LHU/LW/LWU and SB/SH/SW IR with signed effective-address offsets.
- [x] Typed LQ/SQ 128-bit transfers with explicit little-endian lane assembly and EE 16-byte address rounding.
- [x] Decode, lift and lower LD/SD 64-bit doubleword memory transfers with 8-byte alignment and explicit low-lane semantics.
- [x] Decode, lift and lower LWL/LWR/SWL/SWR and LDL/LDR/SDL/SDR with checked aligned reads/writes and byte-lane merge semantics.
- [x] Main-RAM backing span, documented aliases, endian-safe accessors, natural-alignment checks and explicit MMIO/unmapped statuses.
- [x] Generated memory code compiled/executed on Windows/MSVC and Linux/GCC for load extension, store width, aliasing, alignment and bounds.
- [x] Generated LQ/SQ code compiled/executed on Windows/MSVC and Linux/GCC for both GPR lanes, 16-byte rounding, little-endian layout and backing-span bounds.
- [x] Generated unaligned word and doubleword merge code exercised across every byte offset with GPR lane-preservation and short-backing failure tests.
- [x] Generated ELF entry-point initializer which validates/copies a companion RAM image, resets GPRs and selects the ELF entry PC.
- [x] Reject allocated REL/RELA relocations and ELF segments outside the implemented default main-RAM mapping.
- [ ] Full EE virtual-memory/TLB mapping, exception delivery, cache semantics and memory-mapped device models.
- [ ] Build manifest with input hash, compiler options, and generated function metadata.
- [x] CFG-aware C++20 dispatcher for the implemented direct and register-indirect control-flow subset.
- [x] Synthetic generated control-flow code compiled and executed on Windows/MSVC and Linux/GCC, including branch-taken, branch-not-taken, delay slots, JAL/JR return, and instruction-budget behaviour.
- [x] ELF-to-CFG-IR-to-C++ command path for the currently supported subset.
- [ ] Validate recompile-elf and the generated RAM image against a lawfully owned PS2 ELF; document translated coverage and unresolved relocations.

**Exit gate:** a synthetic instruction block can be lifted, verified, lowered, compiled, and produce the expected guest-state transitions. This validates the compiler slice, not a retail title.

## M4 — First real-title native execution slice

- [ ] Analyze a developer-supplied local ELF from a lawfully owned ISO; keep the ISO/ELF and extracted game material outside Git.
- [ ] Discover and translate a real code region and its direct callees, with coverage and unresolved edges recorded.
- [ ] Implement only the EE/runtime services required by that observed execution path.
- [ ] Produce a native title module linked to the shared runtime ABI; record all unresolved instructions, relocations, and services.
- [ ] Capture traces and an exact repeatable guest-visible boot/entry stage, including the disc digest and test configuration.

**Exit gate:** native-recompiled code from one real title executes through the shared runtime and reaches an evidenced, repeatable stage. This is not yet a playable-game claim or proof of multi-game compatibility.

## M5 — Shared PS2 runtime subsystem expansion

Runtime work is driven by real-title traces, public behavioral evidence, and differential tests. Implement subsystem boundaries independently; do not return fake success for unsupported hardware operations.

- [ ] EE exceptions/interrupt delivery, COP0/TLB translation, cache semantics, timers, scratchpad, and memory visibility.
- [ ] EE DMA, SIF/SIF RPC, and memory-mapped device/register models with explicit timing/synchronization contracts.
- [ ] VU0 macro-mode and VU0/VU1 microprogram compilation/execution, vector memory, VIF command processing, and synchronization.
- [ ] IOP/R3000A execution, IOP RAM, IRX loading, and HLE kernel/service surface.
- [ ] GIF packet processing, GS registers/state/local memory, rendering, frame presentation, and synchronization.
- [ ] SPU2/audio, controller, optical media, and memory-card services.
- [ ] Clean-room HLE boot/kernel-facing behavior sufficient for supported titles without requiring users to provide a proprietary BIOS image; document services that remain unavailable.
- [ ] Host integration for window/input/audio, save storage, diagnostics, and deterministic session shutdown.

**Exit gate:** the selected real-title path advances through its required EE, VU, IOP, graphics, media, input, and audio interactions without unexplained success fallbacks. Scope is evidence-driven; not every PS2 subsystem must be implemented before the first boot slice.

## M6 — Multi-game compatibility, performance, and release engineering

- [ ] Select a second genuinely different title as a compatibility target and record its exact serial/revision/disc digest and legal test provenance.
- [ ] Run at least two different titles through the same title-agnostic ISO/ELF frontend, decoder, IR, native backend, and shared runtime ABI.
- [ ] Reach repeatable, independently evidenced native boot-tested stages in both titles; isolate legitimate title-specific service profiles without forking shared semantics.
- [ ] Maintain a compatibility catalog keyed by title identity/revision and disc digest, with exact REPS2/runtime/host configuration, evidence, unsupported coverage, and known limitations; store no copyrighted binaries.
- [ ] Differential tests for implemented EE/VU/IOP/DMA/SIF/GIF/GS/audio/input behavior against public behavioral references or an independent oracle where available.
- [ ] Native profiling and correctness-preserving code-generation/runtime optimization, measured against recorded workloads.
- [ ] Reproducible build artifacts, provenance, regression automation, crash diagnostics, and release-quality test reports.
- [ ] Define representative gameplay acceptance plans; do not mark a title Playable based only on boot, menus, or synthetic tests.

**Exit gate:** at least two distinct titles reach repeatable native boot-tested stages through the shared platform path; compatibility records are reproducible and disclose remaining blockers. A first public multi-game preview may expose different compatibility states, but a stable multi-game release targets at least two titles independently validated as Playable.

## M7 — Windows multi-game desktop platform

Build the end-user experience on the same library/session APIs exercised by tests and the developer CLI. The UI is the product surface, not a substitute for the runtime or compatibility gates.

- [ ] Persistent library service with add/remove/relink/rescan operations, path validation, integrity checks, and versioned schema migrations.
- [ ] End-user Add Game flow accepting a single owned ISO and orchestrating discovery, internal extraction, analysis, native build, registration, and clear progress/errors without manual BOOT.ELF or developer CLI steps.
- [ ] Controller-first Windows desktop shell with a Big Picture-style library grid/list, navigation, search/filter, metadata, optional cover art, and accessible keyboard/mouse operation.
- [ ] Per-game build cache keyed by ISO content digest, frontend/compiler and IR/manifest versions, runtime ABI, host target, compiler identity, and relevant build configuration; reject stale or mismatched artifacts.
- [ ] Session manager launching a native title module with the correct shared runtime profile; isolate per-game settings, saves, logs, crash reports, and clean shutdown.
- [ ] Play/Stop, compatibility status, build/launch diagnostics, save paths, and recovery/relink flows; network metadata services remain optional and library operation must work offline.
- [ ] The normal user workflow does not require separately locating proprietary BIOS firmware. Communicate unsupported HLE/runtime behavior honestly.
- [ ] Keep CLI as an internal developer and automation interface; GUI and CLI call the same underlying services.

**Exit gate:** a user can add a game using only its ISO, see truthful lifecycle/compatibility states in the Windows library, and launch supported native modules through the shared runtime. The library contains multiple independently imported titles; at least two are boot-tested to preserve the multi-game proof. Stable release readiness additionally requires the M6 playable-title target and documented limitations.
