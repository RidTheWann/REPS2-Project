# ADR 0021: COP1 Single-Precision Arithmetic on the EE's Non-IEEE Unit

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** R5900 decoder, instruction-effect metadata, typed IR, C++20 AOT backend, runtime FPU numeric core and state ABI, differential vectors, and the native arithmetic fixtures

## Context

The COP1 transfer slice (ADR 0020) moved every retail entry scan from 34 to 66 decoded words and left every title stopping on the same word: `0x46010018`, primary `0x11`, format `0x10`, funct `0x18` - `ADDA.S`. Arithmetic, not data movement, became the largest unsupported FPU population: 385,468 `cop1` words across the eight census images, with ADD/SUB/MUL/DIV/MOV/CVT.S.W alone accounting for over 200,000.

This increment covers the S-class and W-class arithmetic whose semantics are pinned by the Sony manuals, and deliberately stops before the COP1 compare family and `BC1` branches, because those change control flow and need a CFG-level condition model rather than one more IR operation.

## Decision

**Implement the EE FPU as the documentation describes it, using integer arithmetic only, and treat every place where sources disagree as a named, testable choice rather than an emulator convention.**

### Operand fields (`rust/frontend/src/mips.rs`)

- `fmt` is bits 25..21; `fs` is bits 15..11, `ft` is bits 20..16, `fd` is bits 10..6, funct is bits 5..0. This is the same placement MFC1 already used for its FPR number, so transfers and arithmetic share one decoder.
- Sony's instruction diagrams and the Ghidra emotionengine SLASPEC place `SQRT.S`'s source in `ft` and reserve `fs`; binutils names `fs` and would reject retail words such as `0x46010004`. The retail census settles it: in 3,977 SQRT.S words, bits 15..11 are zero **100%** of the time and bits 20..16 are non-zero 35.5% of the time. The same test confirms `ft` is reserved for ABS/MOV/NEG/CVT.W.S, `fd` is reserved for the five accumulate forms, and no compare form writes `fd`.
- Non-zero reserved fields decode to `Opcode::Unknown` instead of being masked away. The instruction masks used are `0xFFE0003F` (ADD/SUB/MUL/DIV/MADD/MSUB/MAX/MIN/RSQRT), `0xFFE0F83F` (SQRT), `0xFFFF003F` (ABS/MOV/NEG/CVT.W.S and W-class CVT.S.W), and `0xFFE007FF` (the accumulate forms).

### Numeric core (`runtime/include/reps2/runtime/ee_fpu.hpp`)

- **No host floating point anywhere.** Every operation decodes a `sign`/`exponent`/`mantissa` triple into integer arithmetic and re-encodes it. The runtime is `constexpr` and compiles on MSVC and GCC without floating-point contraction or rounding-mode dependence.
- The EE format is **not IEEE single**: biased exponent `0xFF` is a normal value, so `±Fmax` is `0x7FFFFFFF`/`0xFFFFFFFF` (not `0x7F7FFFFF`); there are no NaN, infinity or denormal encodings; `+0 == -0`; rounding is fixed toward zero with no guard/round/sticky bits. Results are truncated to the representable grid at each operation.
- `fpu_sub` cannot be expressed as `add(a, flip(b))` in one case: the zero-result sign follows the **minuend**, per Table 8-6, so `0 - 0` preserves the first operand's sign even though `0 + (-0)` would not.
- Addition selects the larger-magnitude operand as the anchor and handles a difference wider than 24 bits explicitly, because that is where the align-and-truncate result stops agreeing with an exact rational computation.
- `fpu_sqrt` scales the radicand so that a single integer square root is the only truncation, which is the largest representable `y` with `y*y <= x`.
- `fpu_div` returns a signed zero and names no flag when the dividend mantissa is zero and the divisor is non-zero: the exact quotient is zero, and neither the DIV.S box nor Table 8-4 names underflow for it. `fpu_mul` already treats a zero operand the same way.
- `fpu_rsqrt` is `fpu_div(fs, fpu_sqrt(ft & 0x7FFFFFFF))`, because the instruction box defines it as `fs / SQRT(|ft|)` while still naming I when `ft` is negative. A zero divisor short-circuits before the square root: the box names D whenever `ft` is zero, and the result is `±Fmax` with the sign of `fs / SQRT(ft)`, so a `-0` divisor flips it. Table 8-5's `0/SQRT(0)` row instead names I; the conflict is recorded below.
- `fpu_cvt_w_s` clamps when the biased exponent exceeds `0x9D`, producing `0x7FFFFFFF`/`0x80000000` and the invalid cause. Table 8-4's "conversion overflow -> ±Fmax, no cause" row is read as an FP-destination overflow, not this integer destination; the instruction box and Table 8-6 give the clamped integer with the invalid flag.
- `fpu_cvt_s_w` interprets its 32-bit operand as a two's-complement integer.
- `fpu_accumulate(acc, a, b, subtract)` computes `acc ± fs*ft`, merging the product's and the sum's flag sets. `fpu_madd`/`fpu_msub` are the two entry points; the `.A` forms (`ADDA/SUBA/MULA/MADDA/MSUBA`) reuse the same arithmetic with ACC as the destination.

### ACC

ACC is a **separate 32-bit register**, not `FPR[32]`; section 8.2 gives it no COP1 register number. `EeState::acc` is therefore only reachable through the accumulate and MADD/MSUB instructions. ADDA/SUBA/MULA overwrite it; MADDA/MSUBA read-modify-write it; MADD/MSUB read it without writing it. Effect metadata exposes `reads_acc`/`writes_acc` so analysis can see the dependency that no register field encodes.

### FCR31 flags

- `apply_fpu_flags(control, result)` implements the per-instruction flag model: causes named by the instruction are set, causes explicitly named as cleared are cleared, all other causes are left alone (the Instruction Set Manual's Ch.5 opening), sticky partners are ORed in and never cleared, and the hard-wired bits are restored.
- The implementation follows the per-instruction boxes over Table 8-2's "otherwise cleared" wording, which cannot be reconciled with the boxes. NEG/MAX/MIN clear O and U; SQRT clears D; every other form leaves unnamed causes untouched. The same priority applies to RSQRT.S, whose box names D for a zero `ft` where Table 8-5 names I.
- `CVT.W.S` sets I (and therefore SI) on a clamp; the Table 8-4/§8.8 conflict is recorded, not hidden.

### Evidence chain

- `tools/ee_fpu_model.py` is an independent arbitrary-precision rational implementation of the same rules. `--self-check` runs 106 hand-derived expectations; `--emit-vectors` writes 425 differential rows.
- `tests/cop1_arith_smoke.cpp` executes all 425 rows through the runtime and also checks FCR31 routing, the cause clears produced by the helpers themselves when every flag is preset, ACC defaults and the control-write mask.
- `tests/cop1_arith_codegen_smoke.cpp` executes a 27-instruction generated block whose expected values are hand-computed from the encodings, not from the oracle: it covers every operand shape, ACC routing, and a real divide-by-zero flag read back through CFC1.
- Mutation evidence for this increment: 16 numeric-core mutants and 10 wiring mutants, all killed. The numeric set covers the overflow threshold, wide-spread addition, the renormalized difference, the Table 8-6 subtraction sign, the sqrt guard width, divide exponent scaling, the sticky shift, unnamed-cause clearing, the CVT.W.S clamp threshold, the NEG/SQRT/MAX/MIN cause clears, the zero-dividend flag, the RSQRT zero-divisor sign, and the CTC1 mask. The wiring set covers decoder field swaps and reserved-bit acceptance, IR operand order, ACC and FPR routing, the raw-result FCSR31 store, and the runtime `MOV.S` sign flip. Two groups are observable in only one layer: the `MOV.S` mutant leaves the Rust suite green and fails only natively, and the four clear-removal mutants are invisible to the vector rows (whose FCSR starts at reset) and fail only the preset-flag helper cases.

### Measured effect

`profile-elf` over the same eight images moves supported words from 6,771,673 (86.66%) to **7,056,375 (90.30%)**, a gain of 284,702 words. Remaining `cop1` drops from 385,468 to 100,766, dominated by the compare functs (`0x32`/`0x34`/`0x36`), `BC1` (format `0x08`), and reserved-field data patterns. `special` is now the largest unsupported family (381,245), followed by `cop1` (100,766) and `cop2` (87,297).

## Open differential cases

These are implemented from the primary or best-available source but have not been confirmed on hardware, so they are listed rather than presented as settled:

- `SUB.S`/`DIV.S` operand order (`fs op ft`). The manual's diagrams and PCSX2 agree; a silicon differential would be needed to prove it.
- The MADD family's ACC convention (`ACC ± fs*ft`). The manual's wording and PCSX2's `FPU.cpp` agree; no silicon differential exists. If the opposite convention is ever proven, the oracle, the 425-row table, the runtime and the fixture change together, which is why the convention is stated once in `fpu_accumulate`.
- A zero dividend raising no flag in `DIV.S`. The DIV.S box names a flag only for a zero divisor, Table 8-4 reserves U for an exponent underflow, and PCSX2's `DIV_S` agrees; no silicon differential exists.
- The RSQRT.S zero-divisor flag conflict. The instruction box names D/SD for any zero `ft`, while Table 8-5's `0/SQRT(0)` rows name I; the box wins, matching how the rest of this slice treats box-versus-table disagreements. The result sign for `fs / SQRT(-0)` is taken from the quotient, which PCSX2's `RSQRT_S` also does.
- Exact toward-zero truncation versus an align-and-truncate implementation. The table already contains the falsifier `sub(1.0, 0.5 + 2^-24) -> 0x3EFFFFFE`; an align-truncate unit would produce `0x3F000000`.
- FCR31 bit 24 as a documented constant-1 field, and `±Fmax` as `0x7FFFFFFF`/`0xFFFFFFFF` rather than the IEEE largest finite value. Both follow the manual and both disagree with some secondary sources.
- The CVT.W.S clamp flag conflict between Table 8-4 and §8.8.
- MADD/MSUB write to `fd` while MADDA/MSUBA write ACC; whether MADDA/MSUBA also update some destination FPR is not claimed.

## Out of scope

COP1 compares (`C.F`/`C.EQ`/`C.LT`/`C.LE` and the rest of funct `0x30..0x3F`), `BC1F`/`BC1T` and their likely forms, and any scheduling use of the FPR/ACC dependency masks remain separate work. The compares set FCR31's condition bit, which needs a `BranchCondition` variant in the CFG layer, so they must land atomically with the branches that consume it.

## References

- Sony EE Core Instruction Set Manual v6.0 Ch.5 (its opening flag-scope note, the COP1 format tables, the DIV.S/RSQRT.S/J formats' per-instruction boxes) and §7.4 (COP1 class encodings).
- Sony EE Core User's Manual v6.0 §8.1-§8.8 (data format, abnormal results including Tables 8-4 to 8-6, ACC, FCR31, conversion, fixed rounding, no FP exceptions).
- GNU binutils 2.45 `opcodes/mips-opc.c` for the base/mask table cross-check.
- `chaoticgd/ghidra-emotionengine-reloaded` `data/languages/cop1.sinc` for the field diagram (and the SQRT.S disagreement with binutils that the retail census resolves).
- PCSX2 `pcsx2/FPU.cpp` as an independent implementation cross-check for `MSUB_S`/`MADD_S` ACC ordering and for the `DIV_S` zero-dividend and `RSQRT_S` zero-divisor paths.
- Related: `docs/decisions/0020-cop1-data-movement.md`, `docs/RESEARCH.md` "R5900 COP1 single-precision arithmetic" and "Measured decoder coverage over lawfully owned retail images".
