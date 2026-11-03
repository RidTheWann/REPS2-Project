# ADR 0053: EE MMI3 POR 128-bit bitwise OR

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** R5900 MMI3 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR semantics

## Context

The current MMI support covers MMI0 PADDW/PSUBW/PADDH/PSUBH, MMI1 PADDUW and QFSRV
with the SA movers, and MMI2 PCPYLD. ADR 0052 recorded the remaining ordering of a
reachable-instruction census across seven lawfully owned retail ELFs: after PCPYLD
(851 words), the next most frequent unsupported MMI operation was **POR with 554
words**, ahead of PEXTLW (489).

POR is the plain 128-bit bitwise OR of two GPRs. It introduces no architectural
state, memory access, exception, shift-amount dependency or control-flow effect, so
it is a bounded next unit that needs no new runtime service.

## Architectural evidence

POR is encoded in the MMI3 group:

- Primary opcode: `MMI = 0x1C`.
- Low six-bit group selector: `MMI3 = 0x29`.
- Five-bit operation selector in bits 10..6: `POR = 0x12`.
- Standard `rs`, `rt` and `rd` fields; no reserved register or immediate field.

The group selector is `0x29`, not `0x39`: the MMI dispatch table routes function
`0x28` to the MMI1 table and `0x29` to the MMI3 table, which is what the existing
`encoding_family` classification in `mips.rs` already assumes.

The operation is

`GPR[rd][127:0] = GPR[rs][127:0] OR GPR[rt][127:0]`

for all 128 bits, with `GPR[0]` hard-wired to zero as for every other destination
write.

Primary references:

- Sony *EE Core Instruction Set Manual*, POR entry
- PS2Tek MMI encoding table (MMI3 selector `0x12`)
- QEMU R5900/TX79 instruction tables, which place POR at MMI3 selector `0x12`
  and mask the encoding to the major opcode plus bits 10..0
- PCSX2 `pcsx2/MMI.cpp`, `POR`

## Decision

### 1. Decode only the exact MMI3 selector

Accept primary opcode `0x1C` (already required by the MMI arm), group selector
`0x29` and operation selector `0x12`. Selector `0x12` also names MMI0 PEXTLW and
MMI2 PAND, so the group selector stays part of the match. The neighboring MMI3
selectors `0x11` (undocumented), `0x13` (PNOR), `0x1A` (PEXCH) and `0x1E` (PEXCW) stay
unknown, and `0x12` in any other MMI group stays unknown.

### 2. Use a typed IR operation

Add `ParallelOr { dst, lhs, rhs }`. The decoder reports both source GPRs as read,
the destination as written, no SA dependency, no memory effect, no synchronous
exception and no control transfer. Register effects therefore reuse the existing
parallel-operation arm rather than growing a second policy.

### 3. Form the complete result before assignment

The runtime helper returns `Gpr128{lhs.low | rhs.low, lhs.high | rhs.high}`.
Generated code assigns the whole 128-bit helper result, so a destination that
aliases `rs`, `rt` or both observes the original operands. Writes to architectural
GPR[0] remain suppressed by the generated guard. POR is commutative, so the emitted
operand order is `(rs, rt)` only to keep the IR readable; no guest-visible behaviour
depends on it, unlike PCPYLD where the operand order selects the destination halves.

## Validation

- Decoder tests assert the exact MMI3 group and operation selectors, the mnemonic,
  the register fields, the `mmi3` family classification, the read/write masks and the
  absence of delay-slot, SA and exception effects. They also assert that selector
  `0x12` in MMI0/MMI1/MMI2 and the MMI3 selectors `0x11`, `0x13`, `0x1A` and `0x1E`
  stay `Unknown`.
- IR tests assert a distinct typed `ParallelOr` operation for a non-zero and a zero
  destination, and that the program verifies.
- Codegen tests assert the emitted `parallel_or` call, the `using` declaration, the
  GPR[0] guard and the aliasing call shape.
- A dedicated native fixture (`reps2_mmi3_por_codegen_smoke`) executes five
  instructions covering a non-aliasing destination, `rd == rs`, `rd == rt`,
  `rd == rs == rt`, and the zero-register destination. The two aliasing cases use
  source values that differ from `r1`/`r2`, so a word that names the wrong source
  register yields a different result instead of matching by accident. The fixture
  asserts hand-derived 128-bit results and that an unrelated register keeps both
  lanes; compile-time assertions check the helper directly, including a split-half
  operand pair that fails if either half is dropped, duplicated or swapped.
- Mutation evidence, checked manually during review (there is no committed harness
  for this slice, unlike the COP1 mutators under `tools/`). Each mutant was built
  and observed to fail: taking the high half from the low lanes, swapping the two
  halves, and returning `rhs` unchanged are each killed by the fixture's static
  assertions; swapping the lifted operand order is killed by the IR and codegen
  tests; widening the selector mask so `0x13` aliases POR is killed by the decoder
  test; and using `0x39` instead of `0x29` for the group selector is killed by the
  decoder, IR and codegen tests.

## Consequences

The census no longer counts these 554 POR words as unsupported. This is a coverage
increment, not a playability claim: the titles still stop at other unsupported
instructions and unimplemented runtime services. The MMI3 siblings (PNOR, PEXCH,
PEXCW, PCPYH, PCPYUD, PINTEH and the MMI3 multiply/divide group), PEXTLW and the
remaining MMI0–MMI3 operations, MMI pipeline-1 state and MMI latency stay out of
scope and are still counted by `profile-elf`.
