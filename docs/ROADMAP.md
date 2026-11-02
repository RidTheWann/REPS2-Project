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
- [x] Coalesce pure BSS PT_LOAD program headers and skip empty segments for SN Systems ProDG linker compatibility; verified against 100% of retail test ELFs (ADR 0024).
- [ ] Full address-space permissions and COP0/TLB mapping.
- [x] Read-only ISO 9660 inspection and SYSTEM.CNF BOOT2/BOOT ELF extraction for raw 2048-byte-sector images; reject unsupported containers and directory features explicitly.
- [x] Content-addressed, per-game ISO import manifest with internally extracted boot ELF and bounded analysis/code-generation status; original ISO remains external and no game binaries are committed.
- [x] Discover ELF entry points and executable STT_FUNC symbol roots.
- [x] Reachability-guided CFG for the implemented branch/jump subset; keep indirect targets unresolved.
- [x] Explicit, validated analysis bounds with developer-CLI overrides (`--max-blocks`, `--max-instructions`, `--max-roots`). Defaults remain 250,000 blocks, 2,000,000 decoded instructions and 500,000 discovered roots; overrides must be positive and stay below ceilings above the physical 32 MiB address space, limit failures name the flag that raises them, and the applied values are recorded in the build manifest (ADR 0030).
- [x] Distinguish file-backed executable bytes from zero-filled PT_LOAD tails during instruction reads.
- [x] Report decoder coverage as an executable-byte census by supported opcode, trap, unsupported encoding family, and exact unsupported word, with a separate bounded entry-point scan; no reachability claim is made.

**Exit gate:** inspect a lawfully owned PS2 ELF and produce a stable JSON manifest plus a map of loadable code/data ranges.

## M2 — R5900 decoder and analysis

- [ ] Cover the documented R5900 integer, 64-bit, MMI0–MMI3, COP0, and FPU instruction families.
- [x] Decode, lift and AOT-lower the COP1 S-class and W-class arithmetic (ADD/SUB/MUL/DIV/SQRT/ABS/NEG/MOV, the `a`-form ACC operations, MADD/MSUB, RSQRT, MAX/MIN, CVT.W.S and CVT.S.W) with the EE's non-IEEE data semantics rather than host `float` behaviour, and model the ACC the arithmetic reads and writes. The operand fields are settled by the retail census (`fs` 15..11, `ft` 20..16, `fd` 10..6), every reserved field is rejected, and a 426-row differential table plus a generated 27-instruction native block pin the results and FCR31 routing, with committed mutation harnesses (20 numeric-core, 10 wiring) behind them. See `docs/decisions/0021-cop1-arithmetic.md`; the open differential cases (operand order, MADD ACC convention and its pseudo-code short circuits, conversion clamp flags, zero-dividend flags, RSQRT zero-divisor flags, signed-zero table rows) are listed there.
- [x] Model the FCR31 condition bit, then decode, lift and AOT-lower the COP1 compares `C.F.S`/`C.EQ.S`/`C.LT.S`/`C.LE.S` and the `BC1F`/`BC1T`/`BC1FL`/`BC1TL` branch family with their delay slots and not-taken annulment. The compare predicates write only bit 23, the branches consume it through a CFG condition, and the non-EE IEEE predicates stay unknown. A 136-row differential table plus a generated four-block native fixture pin the signed-zero, exponent-0, top-of-range and branch-path behavior; the committed wiring harness kills 21 mutants including native-only compare and BC1 cases. See `docs/decisions/0022-cop1-compare-and-bc1.md`; `C.F.S` has no retail population and the condition-only flag model remains an open differential case.
- [x] Decode, lift, and AOT-lower the non-trapping 64-bit scalar subset (DADDU, DSUBU, DADDIU, DSLL/DSRL/DSRA and their 32-shift variants); test modulo arithmetic, sign-extended immediates, shift boundaries, and upper GPR-lane preservation.
- [x] Decode, lift, and AOT-lower register-variable SLLV/SRLV/SRAV and DSLLV/DSRLV/DSRAV; test 5-bit/6-bit shift-count masking, word sign extension, arithmetic sign fill, and GPR upper-lane preservation through compiled native code.
- [x] Expose conservative GPR/HI-LO, memory, control-flow, and possible-exception metadata for currently decoded opcodes; unknown encodings remain opaque.
- [x] Decode R5900 three-operand MULT/MULTU with optional `rd`, plus base EE MTHI/MTLO/MFHI/MFLO; division, MMI pipeline-1, and full instruction-family coverage remain separate work.
- [x] Decode, lift, and AOT-lower integer division (DIV/DIVU) and conditional moves (MOVN/MOVZ) alongside memory barriers (SYNC) and cache/prefetch hints (CACHE/PREF). Division handles R5900 divide-by-zero (sign-dependent for DIV, UINT32_MAX for DIVU) and INT32_MIN / -1 overflow without host undefined behavior; conditional moves preserve unaffected destination bits and GPR upper lanes. Verified via 12-test CMake CTest suite including reps2_div_mov_codegen_smoke; linear entry scan progresses past SYNC across all retail titles to the first SYSCALL trap.
- [x] Decode, lift, and AOT-lower COP0 register transfers (MFC0/MTC0) and interrupt controls (EI/DI/ERET). Modeled PRId as read-only constant (0x00002E20), Status register with EIE bit 16 manipulation, Count and Status round-tripping, GPR[0] write suppression, and 64-bit upper-lane preservation. Verified via reps2_cop0_codegen_smoke in 13-test CMake CTest suite; eliminated 444 unsupported instructions in retail Black (ADR 0025).
- [x] Decode, lift, and AOT-lower the MMI1-group PADDUW operation with independent unsigned 32-bit lane addition and lane-local saturation; other MMI operations and MMI pipeline-1 state remain separate work.
- [x] Decode and lift the MMI shift-amount movers MFSA/MTSA/MTSAB/MTSAH with their documented reserved-field masks, and lower MMI1 QFSRV so SA is consumed as a 64-bit bit count; branch-likely and three-cycle SA hazards remain unmodeled.
- [x] Decode, lift and AOT-lower the COP1 transfers MFC1/MTC1/CFC1/CTC1 plus LWC1/SWC1 using the verified bits 25..21 format selector and reserved bits 10..0, with decoder-side FPR/FCR dependency metadata (read and write masks are computed and unit-tested but not yet consumed by any scheduling or analysis pass), sign-extended GPR writes, program-uncontrolled FCR31 rounding bits, and rejection of the double-width and reserved control encodings the EE does not implement. Arithmetic is covered by the next item; BC1 branches are covered by the compare/BC1 item above.
- [x] Decode and lift NOR, SLT, SLTU, SLTI and SLTIU into typed scalar IR; SLTIU's immediate is sign-extended before unsigned comparison.
- [x] Decode, lift and AOT-lower trapping 32-bit ADD/ADDI/SUB and 64-bit DADD/DSUB/DADDI separately from their unsigned counterparts; deliver Cause.ExcCode 12 through the Level-1 EE exception path with no destination commit, including GPR[0] and delay-slot overflow.
- [x] Delay-slot-aware basic block and CFG construction for a bounded control-transfer subset.
- [x] Decode, lift and AOT-lower R5900 branch-likely BEQL/BNEL/BLEZL/BGTZL and BLTZL/BGEZL/BLTZALL/BGEZALL with not-taken delay-slot annulment; verify taken/not-taken side effects and unconditional PC+8 link writes in generated native code.
- [x] Direct branch/call recovery and unresolved indirect-edge tracking.
- [x] Entry/function root discovery from ELF entry and STT_FUNC symbols, plus opt-in static discovery of `JAL` call targets and canonical `addiu $sp, $sp, -N` prologues for stripped retail executables. Discovered roots are labeled candidates rather than reachability claims; heuristic-only blocks that cannot decode are skipped and reported while authoritative roots still fail hard. Eight retail ELFs complete without truncation. See `docs/decisions/0026-static-function-discovery.md`.
- [ ] Full documented R5900 branch, branch-likely, and control-transfer coverage.
- [ ] Differential instruction corpus against documented behavior and an external oracle.

**Exit gate:** full instruction coverage report, semantic test suite, and CFG output for real ELF code regions. The current bounded CFG implementation is an intermediate milestone, not completion of M2.

## M3 — IR and generated-code vertical slice

- [x] Typed Rust IR for a documented straight-line scalar subset.
- [x] IR verifier for register, PC alignment/sequencing, and word-shift invariants.
- [x] Deterministic C++20 source generation for the implemented IR operations.
- [x] Synthetic generated C++ compiled and executed against the minimal EE state ABI on Windows and Linux CI.
- [x] AOT-lower and execute MTHI/MTLO/MFHI/MFLO and signed/unsigned R5900 MULT/MULTU in a dedicated native smoke test on Windows/MSVC and Linux/GCC, including HI/LO and `rd` results.
- [x] AOT-lower and execute NOR/SLT/SLTU/SLTI/SLTIU with signed 64-bit boundary, unsigned ordering, negative immediate, GPR[0], and upper-lane preservation tests on Windows/MSVC and Linux/GCC.
- [x] AOT-lower and execute PADDUW across all four 128-bit lanes in a native smoke test, including lane-local unsigned saturation, a non-zero destination, and GPR[0] suppression.
- [x] AOT-lower and execute MFSA/MTSA/MTSAB/MTSAH with QFSRV in one native smoke test, covering the 64-bit SA round-trip, the `*8`/`*16` bit-count scaling, funnel shifts at SA = 88/64/0, SA staying unread-consumed by QFSRV, full 128-bit destination writes, and GPR[0] suppression observed while SA is non-zero.
- [x] AOT-lower and execute the COP1 transfers in one native smoke test, covering MTC1 32-bit truncation, MFC1/CFC1 sign extension with GPR upper-lane preservation, GPR[0] suppression while FPR[6] is non-zero, FPR[0] written by both MTC1 and LWC1, an SWC1/LWC1 round trip through main RAM at positive and negative offsets, FIR read-back asserted against the literal `0x2E00` rather than the constant the emitter reads, and CTC1 leaving FCR31 bits 1..0 hard-wired to `0b01`. Eight mutations of the emitters, the control-register mask, the FIR constant and the lift mapping each make the fixture fail.
- [x] AOT-lower and execute the COP1 S/W-class arithmetic in a generated 27-instruction block on Windows/MSVC and Linux/GCC, with expected values hand-derived from the encodings rather than from the oracle: every operand shape, ACC routing through ADDA/MADD/MSUB/MULA/MADDA/MSUBA, and a divide-by-zero cause read back through CFC1. The runtime core is separately checked against the 426-row differential table and against preset-flag cases that prove each helper's resolved cause clears.
- [x] AOT-lower and execute the COP1 compares and BC1 branches in generated native C++ on Windows/MSVC and Linux/GCC: taken and not-taken `BC1T`, `BC1F`, taken and annulled `BC1FL`/`BC1TL`, delay-slot execution, condition-bit-only FCSR updates with unrelated causes/stickies preserved, and signed-zero equality. The runtime is separately checked against the 136-row compare table and its exact row count.
- [x] AOT-lower and execute COP0 register transfers (MFC0/MTC0) and EI/DI status operations in native C++ on Windows/MSVC, validating lane preservation and read-only PRId invariants (reps2_cop0_codegen_smoke).
- [x] Execute DADD/DSUB/DADDI success and overflow paths in generated native C++ on Windows/MSVC and Linux/GCC, including no-write-on-trap and overflow when `rd = 0`.
- [x] CFG-aware typed IR for supported branch/jump terminators, with explicit scalar delay-slot operations.
- [x] Typed LB/LBU/LH/LHU/LW/LWU and SB/SH/SW IR with signed effective-address offsets.
- [x] Typed LQ/SQ 128-bit transfers with explicit little-endian lane assembly and EE 16-byte address rounding.
- [x] Decode, lift and lower LD/SD 64-bit doubleword memory transfers with 8-byte alignment and explicit low-lane semantics.
- [x] Decode, lift and lower LWL/LWR/SWL/SWR and LDL/LDR/SDL/SDR with checked aligned reads/writes and byte-lane merge semantics.
- [x] Main-RAM backing span, documented aliases, endian-safe accessors, natural-alignment checks and explicit MMIO/unmapped statuses.
- [x] Generated memory code compiled/executed on Windows/MSVC and Linux/GCC for load extension, store width, aliasing, alignment and bounds.
- [x] Deliver precise EE AdEL/AdES for checked natural-alignment failures in generated scalar and FPU word loads/stores, with BadVAddr, Cause.ExcCode, no partial destination/memory commit and branch-delay EPC/BD validation on Windows/MSVC and Linux/GCC (ADR 0035).
- [x] Generated LQ/SQ code compiled/executed on Windows/MSVC and Linux/GCC for both GPR lanes, 16-byte rounding, little-endian layout and backing-span bounds.
- [x] Generated unaligned word and doubleword merge code exercised across every byte offset with GPR lane-preservation and short-backing failure tests.
- [x] Generated ELF entry-point initializer which validates/copies a companion RAM image, resets GPRs and selects the ELF entry PC.
- [x] Reject allocated REL/RELA relocations and ELF segments outside the implemented default main-RAM mapping.
- [ ] Full EE virtual-memory/TLB mapping and complete exception coverage beyond the implemented Level-1 SYSCALL/BREAK/OVERFLOW/AdEL/AdES subset, plus cache semantics and memory-mapped device models.
- [x] Split the emitted CFG into multiple C++ translation units with a shared dispatch/ABI so discovered retail executables (hundreds of thousands of blocks, ~178 MB as one unit for Black/USA) can be built natively. Each block is now a separate `block_0xXXXXXXXX` function dispatched through a sorted table with `std::lower_bound`; `--tu-count N` emits a dispatcher plus `N` part files. Black/USA as 1 dispatcher + 8 parts compiles and links with MSVC `/O2 /W4 /permissive-` in ~149 s sequentially with zero warnings, and a runtime probe confirms block-atomic budget behavior across units (ADR 0027, `reps2_cfg_split_codegen_smoke`).
- [x] Build manifest schema v2 with input SHA-256, tool/host provenance, analysis limits, discovery totals, program counts, separate `unsupported_stop_count` and `guest_exception_count`, and translation-unit layout. `<stem>.manifest.json` is deterministic and timestamp-free, so identical invocations produce byte-identical output; it always records `playable: false`. Schema v1 remains identifiable for older artifacts; v2 makes the new exception accounting explicit (ADR 0033).
- [x] Deterministic native build project: `--native-build` writes `<stem>.native/CMakeLists.txt` and a host `probe_main.cpp` that compile the dispatcher, every part and the probe against an explicit `REPS2_RUNTIME_INCLUDE`, then register a CTest that loads the RAM image (when present) and performs one bounded `execute_cfg` call. The renderer embeds no absolute paths or timestamps and the frontend still never invokes or guesses a C++ compiler; a nested CTest configures, builds and runs the generated project end to end (ADR 0029).
- [x] Record the configured native compiler identity (ID/version/target), host and target platform, CMake generator/version, build configuration, C++ standard, and global/per-configuration compile and linker flags in a separate reps2-native-toolchain-<config>.json emitted by the generated CMake project. This report is build-directory output, not part of the timestamp-free frontend manifest; the frontend still never invokes or guesses a C++ compiler (ADR 0032).
- [ ] Derive a stable host-toolchain fingerprint and include it, the runtime ABI, IR/manifest schema, and relevant build configuration in the per-game native build-cache key.
- [x] CFG-aware C++20 dispatcher for the implemented direct and register-indirect control-flow subset.
- [x] Synthetic generated control-flow code compiled and executed on Windows/MSVC and Linux/GCC, including branch-taken, branch-not-taken, delay slots, JAL/JR return, and instruction-budget behaviour.
- [x] Deliver the EE Level-1 SYSCALL/BREAK/OVERFLOW exception path: generated blocks set `Cause.ExcCode` (8/9/12), save EPC and BD when EXL is clear, set EXL, and enter the correct general exception vector (`0x80000180`, or `0xBFC00380` with BEV); `0x80000200` remains the separate interrupt vector. Nested Level-1 exceptions update ExcCode but preserve the original EPC/BD frame. ERET is a typed control terminator that resumes through EPC or ErrorEPC and clears the corresponding EXL/ERL bit; ambiguous EXL+ERL and empty return contexts fail explicitly. Windows/Linux native smoke tests cover both exception codes, vectors, nested/delay-slot frame rules, and execution through generated ERET. CFG analysis and native lowering admit SYSCALL/BREAK in executed delay slots; branch-likely annulment still suppresses unexecuted traps, while signed arithmetic overflow in an admitted delay-slot operation is delivered with BD/EPC precision and branch-likely annulment is preserved. AdEL/AdES data-alignment faults also deliver ExcCode 4/5 with BadVAddr and precise EPC/BD; only checked data-alignment faults are promoted, while TLB/unmapped/MMIO failures remain distinct. BIOS/HLE services, instruction-fetch faults, other exception causes, and the wider TLB/interrupt model remain unimplemented (ADR 0035).
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
