# ADR 0055: EE MMI0 PPACW and MMI3 PCPYUD word pack/copy

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** R5900 MMI0/MMI3 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR semantics

## Context

The file-backed census over seven lawfully owned retail ELFs (deduplicated by
file size) ranked the remaining unsupported MMI operations after PEXTLW
(489 words) as `0x08/0x13` (649 words), `0x09/0x1F` (648 words, PROT3W),
`0x28/0x18` (358 words, PADDUB), `0x09/0x1E` (356 words, PEXEW),
`0x29/0x0E` (355 words, PCPYUD), `0x08/0x1A` (307 words, PEXTLB) and
`0x09/0x12` (238 words, PAND). This increment implements the two
data-movement operations with the highest word counts that are pure copies or
packs of whole doublewords/words and that reuse the existing two-source parallel
effect policy without introducing a reserved register field or new runtime
service:

- MMI0 PPACW (`0x08/0x13`, 649 words): packs the low word of each 64-bit half
  of its two sources.
- MMI3 PCPYUD (`0x29/0x0E`, 355 words): copies the upper doubleword of each
  source.

Together these remove 1,004 words from the census and complete the pack/copy
family alongside the already implemented PEXTLW, PCPYLD and POR.

## Architectural evidence

Both operations are encoded under primary opcode `0x1C` (MMI):

- PPACW: low six-bit group selector `MMI0 = 0x08`, five-bit operation
  selector in bits 10..6 `0x13`, standard `rs`, `rt`, `rd`, no reserved field.
- PCPYUD: low six-bit group selector `MMI3 = 0x29`, five-bit operation
  selector in bits 10..6 `0x0E`, standard `rs`, `rt`, `rd`, no reserved field.

Selector `0x13` also names MMI2 PXOR and MMI3 PNOR, and selector `0x0E` also
names MMI2 PCPYLD, so the group selector stays part of the match. The neighboring
MMI0 selectors `0x11` (PSUBSW), `0x14` (PADDSH), `0x15` (PSUBSH), `0x1A`
(PEXTLB) and `0x1B` (PPACB) stay unknown, as do the neighboring MMI3 selectors
`0x0C` (PMULTUW), `0x0D` (PDIVUW) and `0x0F`.

Semantics, agreed across the Sony EE Core Instruction Set Manual, the PS2Tek
MMI encoding table, QEMU `target/mips/tcg/tx79-translate.c` (both `trans_PPACW`
and `trans_PCPYUD`), PCSX2 `pcsx2/MMI.cpp` (`PPACW`, `PCPYUD`) and the PCSX2
recompiler `pcsx2/x86/iMMI.cpp`:

- PPACW:
  - `GPR[rd][31:0] = GPR[rt][31:0]`
  - `GPR[rd][63:32] = GPR[rt][95:64]`
  - `GPR[rd][95:64] = GPR[rs][31:0]`
  - `GPR[rd][127:96] = GPR[rs][95:64]`
  - result words `[rt0, rt2, rs0, rs2]`; the second word of each result
    doubleword is the low word of the *same* source's upper doubleword.
- PCPYUD:
  - `GPR[rd][63:0] = GPR[rs][127:64]`
  - `GPR[rd][127:64] = GPR[rt][127:64]`
  - result doublewords `[rs.high, rt.high]`. Unlike MMI2 PCPYLD the halves are
    not crossed, so the first operand supplies the low half.

Neither operation saturates, raises an exception or touches any architectural
state other than the destination GPR. Neither reads the second or fourth word of
any source.

## Decision

### 1. Decode only the exact group selectors

Accept PPACW under group `0x08` selector `0x13` and PCPYUD under group `0x29`
selector `0x0E`. The shared selectors in other groups remain their existing
operations (PXOR, PNOR, PCPYLD) and the neighboring selectors stay unknown.

### 2. Use typed IR operations

Add `ParallelPackWord { dst, lhs, rhs }` and `ParallelCopyUpperDoubleword { dst, lhs, rhs }`.
The decoder reports both source GPRs as read, the destination as written, no SA
dependency, no memory effect, no synchronous exception and no control transfer.
Both therefore reuse the existing parallel-operation effect arm.

### 3. Form the complete result before assignment

The runtime helpers return:

- `parallel_pack_word(lhs, rhs) = Gpr128{ rhs.low_word0 | (rhs.high_word0 << 32),
  lhs.low_word0 | (lhs.high_word0 << 32) }`
- `parallel_copy_upper_doubleword(lhs, rhs) = Gpr128{ lhs.high, rhs.high }`

Generated code assigns the whole 128-bit helper result, so a destination that
aliases `rs`, `rt` or both observes the original operands. Writes to
architectural GPR[0] remain suppressed by the generated guard. PPACW is not
commutative: the first operand supplies the result's high half. PCPYUD is not
commutative either, and it is not a mirror of PCPYLD (PCPYLD crosses the halves,
PCPYUD does not).

## Validation

- Decoder tests assert the exact group and operation selectors, the mnemonics
  `ppacw`/`pcpyud`, the register fields, the `Mmi0`/`Mmi3` family
  classification, the read/write masks and the absence of delay-slot, SA and
  exception effects. They also assert that the same selectors in other groups
  stay distinct (PXOR, PNOR, PCPYLD) and that the neighboring selectors stay
  unknown, including the explicit cross-group assertions that selector `0x13` in
  MMI1/MMI2/MMI3 stays unknown and that selector `0x0E` in MMI0/MMI1 stays
  unknown.
- IR tests assert distinct typed operations for a non-zero and a zero
  destination for both ops, and that the program verifies.
- Codegen tests assert the emitted helper calls, the `using` declarations, the
  GPR[0] guard, the aliasing call shapes and that PPACW is not lowered as
  PEXTLW and PCPYUD is not lowered as PCPYLD.
- Dedicated native fixtures (`reps2_mmi0_ppacw_codegen_smoke`,
  `reps2_mmi3_pcpyud_codegen_smoke`) execute five instructions each, covering
  a non-aliasing destination, `rd == rs`, `rd == rt`, `rd == rs == rt`, and
  the zero-register destination. The PPACW fixture asserts hand-derived 128-bit
  results, that the upper doublewords are read, that both low doublewords are
  ignored, and that both helpers are distinct from their siblings. The PCPYUD
  fixture asserts hand-derived 128-bit results, that the low doublewords are
  ignored, that the operands are not crossed and that the helper is distinct
  from PCPYLD. Compile-time assertions check the helpers directly.
- Manual mutation evidence: swapping the two result words within a doubleword,
  swapping the lifted operands, reading the high word instead of the low word of
  each doubleword, crossing the halves in PCPYUD, and widening the selector
  masks were each built and observed to fail.

## Consequences

The census no longer counts these 1,004 words as unsupported. This is a
coverage increment, not a playability claim. The remaining high-frequency MMI
operations (PROT3W 648, PADDUB 358, PEXEW 356, PEXTLB 307, PAND 238, PNOR 196)
and the rest of the MMI0-MMI3 set, MMI pipeline-1 state and MMI latency stay
out of scope and are still counted by `profile-elf`. In particular PROT3W and
PEXEW are single-source word permutations whose reserved `rs` field still needs
its own decode constraint and IR shape.
