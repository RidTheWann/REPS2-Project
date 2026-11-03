# ADR 0054: EE MMI0 PEXTLW low-word interleave

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** R5900 MMI0 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR semantics

## Context

The current MMI support covers MMI0 PADDW/PSUBW/PADDH/PSUBH, MMI1 PADDUW and QFSRV
with the SA movers, MMI2 PCPYLD, and MMI3 POR. ADRs 0052 and 0053 recorded the
reachable-instruction census across seven lawfully owned retail ELFs: after PCPYLD
(851 words) and POR (554 words), the next most frequent unsupported MMI operation was
**PEXTLW with 489 words**.

PEXTLW is a pure data movement across the low doublewords of two GPRs. It introduces
no architectural state, memory access, exception, shift-amount dependency or
control-flow effect, so it is a bounded next unit that needs no new runtime service.

## Architectural evidence

PEXTLW is encoded in the MMI0 group:

- Primary opcode: `MMI = 0x1C`.
- Low six-bit group selector: `MMI0 = 0x08`.
- Five-bit operation selector in bits 10..6: `PEXTLW = 0x12`.
- Standard `rs`, `rt` and `rd` fields; no reserved register or immediate field.

Selector `0x12` is shared with MMI2 PAND and MMI3 POR, so the group selector must stay
part of the match. The MMI dispatch table routes function `0x08` to MMI0, which is what
the existing `encoding_family` classification in `mips.rs` already assumes.

The operation is

`GPR[rd][31:0] = GPR[rt][31:0]`,
`GPR[rd][63:32] = GPR[rs][31:0]`,
`GPR[rd][95:64] = GPR[rt][63:32]`,
`GPR[rd][127:96] = GPR[rs][63:32]`

so the result words are `[rt0, rs0, rt1, rs1]`. Both source high doublewords
(`[127:64]`) are ignored, and `GPR[0]` remains hard-wired to zero as for every other
destination write.

Primary references:

- Sony *EE Core Instruction Set Manual*, PEXTLW entry
- PS2Tek MMI encoding table (MMI0 selector `0x12`)
- QEMU R5900/TX79 instruction tables, which place PEXTLW at MMI0 selector `0x12`
  and mask the encoding to the major opcode plus bits 10..0
- PCSX2 `pcsx2/MMI.cpp`, `PEXTLW`
- `ps2homebrew/ps2-unpacker` `MMI.c`, `PEXTLW`

QEMU's `gen_pextw` helper was **not** used as semantic evidence: its upper-half
construction appears inconsistent with PCSX2's word ordering. QEMU's encoding table was
used for encoding confirmation only. The two behavioural references agree on the
`[rt0, rs0, rt1, rs1]` layout and on ignoring both upper doublewords.

## Decision

### 1. Decode only the exact MMI0 selector

Accept primary opcode `0x1C` (already required by the MMI arm), group selector `0x08`
and operation selector `0x12`. MMI3 selector `0x12` stays `POR` and MMI2 selector
`0x12` (`PAND`) stays unknown in this increment. The neighboring MMI0 selectors `0x11`
(PSUBSW), `0x13` (PPACW), `0x1A` (PEXT5) and `0x1B` (PPAC5) stay unknown.

### 2. Use a typed IR operation

Add `ParallelExtendLowWord { dst, lhs, rhs }`. The decoder reports both source GPRs as
read, the destination as written, no SA dependency, no memory effect, no synchronous
exception and no control transfer. Register effects therefore reuse the existing
parallel-operation arm rather than growing a second policy.

### 3. Form the complete result before assignment

The runtime helper returns

`Gpr128{(rhs0 << 32) | lhs0, (rhs1 << 32) | lhs1}`

where `lhsN`/`rhsN` are the low and high words of the low doubleword. Generated code
assigns the whole 128-bit helper result, so a destination that aliases `rs`, `rt` or
both observes the original operands. Writes to architectural GPR[0] remain suppressed
by the generated guard. PEXTLW is not commutative, so the emitted operand order
`(rs, rt)` is guest-visible and is asserted by the tests.

## Validation

- Decoder tests assert the exact MMI0 group and operation selectors, the mnemonic,
  the register fields, the `mmi0` family classification, the read/write masks and the
  absence of delay-slot, SA and exception effects. They also assert that selector
  `0x12` in MMI1/MMI2 stays `Unknown`, that MMI3 selector `0x12` stays `POR`, and that
  the neighboring MMI0 selectors `0x11`, `0x13`, `0x1A` and `0x1B` stay `Unknown`. The
  existing POR test was updated so its former MMI0-unknown expectation now names
  PEXTLW.
- IR tests assert a distinct typed `ParallelExtendLowWord` operation for a non-zero and
  a zero destination, and that the program verifies.
- Codegen tests assert the emitted `parallel_extend_low_word` call, the `using`
  declaration, the GPR[0] guard and the aliasing call shape.
- A dedicated native fixture (`reps2_mmi0_pextlw_codegen_smoke`) executes five
  instructions covering a non-aliasing destination, `rd == rs`, `rd == rt`,
  `rd == rs == rt`, and the zero-register destination. The aliasing cases use source
  values that differ from `r1`/`r2`, so a word that names the wrong source register
  yields a different result instead of matching by accident. The fixture asserts
  hand-derived 128-bit results, that both high doublewords are ignored, that the
  swapped operand order differs, and that an unrelated register keeps both lanes.
  Compile-time assertions check the helper directly.
- Mutation evidence, checked manually during review (there is no committed harness for
  this slice, unlike the COP1 mutators under `tools/`). Each mutant was built and
  observed to fail: reading the high doubleword, swapping the two result words within
  a doubleword, swapping the lifted operands, widening the selector mask so `0x13`
  aliases PEXTLW, and using MMI3 group `0x29` instead of `0x08`.

## Consequences

The census no longer counts these 489 PEXTLW words as unsupported. This is a coverage
increment, not a playability claim: the titles still stop at other unsupported
instructions and unimplemented runtime services. The MMI0 siblings (PAND, PXOR, PNOR,
PSUBW saturation forms, PPACW, PEXT5, PPAC5, PADDSW/PSUBSW), the remaining MMI0–MMI3
operations, MMI pipeline-1 state and MMI latency stay out of scope and are still
counted by `profile-elf`.
