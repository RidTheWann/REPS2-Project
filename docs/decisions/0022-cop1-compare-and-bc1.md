# ADR 0022: COP1 Compares, the FCR31 Condition Bit, and BC1 Branches

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** R5900 decoder, instruction-effect metadata, CFG branch conditions, typed IR, C++20 AOT backend, runtime FPU condition helper, differential compare vectors, and the generated control-flow fixture

## Context

ADR 0021 left the COP1 compare family and `BC1` out of scope on purpose. The arithmetic slice had already modeled FCR31's cause and sticky fields, but not the condition bit (bit 23), and the compare instructions write that bit while `BC1F`/`BC1T` consume it. Splitting the compares from the branches would create a tree where a compare could be lifted but no generated branch could read its result, or a branch could be emitted against a bit that no supported instruction ever wrote. The two families are therefore one atomic slice.

The census made the size of the slice concrete. After the arithmetic slice, the remaining `cop1` family was 100,766 words, and its largest components were the compare functs and `BC1`. The entry scan had already moved past the FPU and stopped on `SYNC`, so this work is about decoder and CFG completeness rather than the next boot blocker.

## Decision

**Model the condition bit as a single FCR31 field with an exact, integer-only comparison, and route both the compare writes and the BC1 reads through the existing CFG branch-condition layer.**

### Decoder (`rust/frontend/src/mips.rs`)

- Only the four predicates the EE defines are accepted: funct `0x30` `C.F.S`, `0x32` `C.EQ.S`, `0x34` `C.LT.S`, and `0x36` `C.LE.S`. The IEEE-only odd functs (`C.UN`, `C.UEQ`, `C.OLT`, `C.ULE`, `C.SEQ`, ...) and `0x38..0x3F` do not exist on the EE and decode to `Opcode::Unknown`.
- The compare form puts `fs` in bits 15..11 and `ft` in bits 20..16, leaves `fd` (bits 10..6) zero, and carries the predicate in funct. The decoder rejects a non-zero `fd` instead of masking it. The retail census agrees: across 44,952 compare words in the eight images, `fd` is zero in every one.
- `BC1` is primary `0x11`, format/rs `0x08`, with the variant in `rt` (`0` `BC1F`, `1` `BC1T`, `2` `BC1FL`, `3` `BC1TL`) and a signed 16-bit word offset in bits 15..0. Selectors `4..31` are reserved and decode to `Unknown`. The offset is sign-extended and multiplied by four before the `PC + 4` addition, matching the other direct branches.
- `BC1F`/`BC1T` and `BC1FL`/`BC1TL` join the existing control-transfer and conditional-branch classifications. `BC1FL`/`BC1TL` join the branch-likely classification, so they reuse the established not-taken delay-slot annulment policy rather than a second implementation.

### Effect metadata

- `C.EQ.S`/`C.LT.S`/`C.LE.S` read `fs` and `ft` and set `writes_fcr`; they write no FPR, no GPR and no ACC.
- `C.F.S` sets `writes_fcr` without claiming either operand as a read, because its result is always false.
- The BC1 family sets `reads_fcr` and `ControlFlowEffect::ConditionalDirect`. It consumes no GPR or FPR.

### Runtime (`runtime/include/reps2/runtime/ee_fpu.hpp`)

- `FpuCompare { False, Equal, LessThan, LessThanOrEqual }` and `fpu_compare` reuse `fpu_detail::compare`, the same exact-rational ordering already used by MAX.S/MIN.S. That gives the EE rules for free: `+0 == -0`, an exponent field of zero decodes as zero rather than an IEEE denormal, exponent `0xFF` is a normal value, and there is no NaN.
- `apply_fpu_condition(control, condition)` is a read-modify-write of exactly bit 23 (`kFpuCondition`, `0x0080'0000`). Causes, stickies, the constant-one bit and the hard-wired rounding bits survive unchanged. A compare names no cause and raises no floating-point exception, so no other field is touched.
- The helper is `constexpr` and uses no host floating point, like the rest of the numeric core.

### Typed IR and backend (`rust/frontend/src/ir.rs`, `codegen.rs`, `analysis.rs`, `control_codegen.rs`)

- `Operation::FpuCompareSingle { lhs, rhs, comparison }` carries the predicate explicitly; `FpuComparison` has one variant per EE predicate. Lifting maps `fs` to `lhs` and `ft` to `rhs`.
- Straight-line codegen emits `state.fcsr = apply_fpu_condition(state.fcsr, fpu_compare(FpuCompare::..., state.fpr[fs], state.fpr[ft]));` and writes no FPR.
- `BranchCondition::FpuCondition { when_set }` is the CFG terminator condition. `BC1F`/`BC1FL` use `when_set: false`; `BC1T`/`BC1TL` use `when_set: true`. The control-codegen expression tests `(state.fcsr & kFpuCondition) != 0` or `== 0` accordingly. The control-IR verifier treats the condition as a state dependency rather than a register operand.

## Evidence

- `tools/ee_fpu_model.py --emit-compare-vectors` writes 136 rows: four predicates over 34 operand pairs covering signed zero, exponent-0 flush, the EE top-of-range order, equal encodings, and mixed signs. `tests/cop1_compare_smoke.cpp` executes every row and asserts the exact row count.
- Each row starts from `0x0101'0041` (C clear, D cause and SI sticky set) and expects the whole FCSR after the compare. A runtime that rebuilds FCR31 from scratch fails every row; a runtime that cannot clear C fails the false rows.
- The smoke test separately starts from every writable bit set and proves that clearing C removes only bit 23 while setting it preserves the rest. It also checks `+0 == -0`, the ordering of the two zeros, and the EE order `IEEE-largest-finite < 2^128 < Fmax`.
- The generated control-flow fixture adds four blocks at `0x7000`, `0x7100`, `0x7200` and `0x7300`: `C.LT.S` + taken/not-taken `BC1T`, `C.F.S` + `BC1F`, `C.EQ.S` + `BC1FL` taken and annulled, and `C.LE.S` + `BC1TL` taken and annulled. The native test asserts the executed path, the delay-slot/annulment behavior, and that unrelated FCSR bits and both FPRs are unchanged. A signed-zero case proves `C.EQ.S` takes the branch for `+0` versus `-0`.
- Rust tests cover the four decode selectors, the reserved functs and `fd`, the BC1 selectors, target arithmetic, annulment metadata, effect masks, IR operand order and comparison variant, and the emitted condition update.
- Mutation evidence: the committed `tools/mutate_cop1_wiring.py` now runs 21 mutants, including four compare/BC1 decoder mutants, three compare IR/codegen mutants, one analysis mutant, one control-codegen mutant, and two runtime mutants. All 21 were killed; the BC1 predicate and control-codegen polarity mutants are invisible to the Rust suite and fail only the native control-flow fixture, and the runtime condition-update mutant fails both the compare vectors and the generated branch.

## Measured effect

`profile-elf` over the same eight images moves supported words from 7,056,375 (90.30%) to **7,146,362 (91.45%)**, a gain of **89,987** words. The compare predicates account for 44,952 of them (`C.EQ.S` 5,884, `C.LT.S` 22,769, `C.LE.S` 16,299, `C.F.S` 0) and the BC1 family for 45,035 (`BC1F` 21,441, `BC1T` 15,686, `BC1FL` 3,856, `BC1TL` 4,052). The seven-distinct-core view moves from 5,757,796 (90.06%) to 5,825,265 (91.11%). The remaining `cop1` family falls from 100,766 to 10,779, which is dominated by data patterns such as `0x44455243` (`"CRED"` as bytes) rather than executable compares. The bounded entry scan is unchanged at 67 decoded words in all eight images and still stops at `0x0000040F`, `SYNC`.

## Open differential cases

- `C.F.S` has **zero** retail population in the eight images. Its decode, effect metadata and clear-the-condition behavior are proven synthetically and by the mutation harness, but not by retail usage.
- The compare flags are modeled as condition-only: no cause or sticky bit is set. The EE has no NaN and no floating-point exception mechanism, and the manual's compare boxes describe the condition update rather than a cause, but no silicon differential exists. If a cause is ever observed, `apply_fpu_condition` and the 136-row table change together.
- The exponent-0 flush and the top-of-range order are inherited from the arithmetic model (ADR 0021). The compare rows pin the same choices at the branch boundary; they do not independently prove them against hardware.
- BC1's not-taken annulment reuses the generic branch-likely rule. The compare/branch fixture proves the intended generated behavior, not the silicon timing of a taken branch's delay slot.

## Out of scope

The other COP1 predicates (`C.UN`, `C.UEQ`, `C.OLT`, `C.ULE`, `C.SEQ`, `C.SQLE`, `C.SLT`, `C.SLE`) are not EE instructions and stay `Unknown`. FPU latency, pipeline hazards, and branch delay-slot scheduling remain unmodeled. The cause and sticky semantics of the arithmetic operations remain in ADR 0021.

## References

- Sony EE Core Instruction Set Manual v6.0 Ch.5 and §7.4 (COP1 format/compare encodings and the BC1 branch forms).
- Sony EE Core User's Manual v6.0 §8.3.2 (FCR31 layout, condition bit 23) and §8.6 Table 8-5 (signed-zero and ordering rules reused by `fpu_detail::compare`).
- GNU binutils 2.45 `opcodes/mips-opc.c` for the base/mask table cross-check.
- `chaoticgd/ghidra-emotionengine-reloaded` `data/languages/cop1.sinc` for the field diagram.
- PCSX2's COP1 compare and BC1 implementations as an independent behavioral cross-check, not as a specification.
- Related: `docs/decisions/0020-cop1-data-movement.md`, `docs/decisions/0021-cop1-arithmetic.md`, `docs/RESEARCH.md` "R5900 COP1 compare and BC1 cross-checks" and "Measured decoder coverage over lawfully owned retail images".
