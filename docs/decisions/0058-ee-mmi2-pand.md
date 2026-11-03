# ADR 0058: EE MMI2 PAND 128-bit bitwise AND

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** R5900 MMI2 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR semantics

## Context

The current MMI support covers MMI0 PADDW/PSUBW/PADDH/PSUBH, MMI0 PEXTLW and
PPACW, MMI1 PADDUW, PADDUB and QFSRV with the SA movers, MMI2 PCPYLD, PEXEW and
PROT3W, and MMI3 POR and PCPYUD. ADR 0057 recorded that the remaining
high-frequency MMI operations after PADDUB include **PAND, PNOR, PXOR and PEXTLB**.

PAND is the plain 128-bit bitwise AND of two GPRs, the MMI2 counterpart of the
MMI3 POR already implemented. It introduces no architectural state, memory
access, exception, shift-amount dependency or control-flow effect, so it is a
bounded next unit that needs no new runtime service.

## Architectural evidence

PAND is encoded in the MMI2 group:

- Primary opcode: `MMI = 0x1C`.
- Low six-bit group selector: `MMI2 = 0x09`.
- Five-bit operation selector in bits 10..6: `PAND = 0x12`.
- Standard `rs`, `rt` and `rd` fields; no reserved register or immediate field.

The group selector is `0x09`: the MMI dispatch table routes function `0x08` to
MMI0, `0x09` to MMI2, `0x28` to MMI1 and `0x29` to MMI3, which is what the
existing `encoding_family` classification in `mips.rs` already assumes. Selector
`0x12` also names MMI0 PEXTLW and MMI3 POR, so the group selector stays part of
the match.

The operation is

`GPR[rd][127:0] = GPR[rs][127:0] AND GPR[rt][127:0]`

for all 128 bits, with `GPR[0]` hard-wired to zero as for every other
destination write.

Primary references:

- Sony *EE Core Instruction Set Manual*, PAND entry
- PS2Tek MMI encoding table (MMI2 selector `0x12`)
- QEMU R5900/TX79 instruction tables, which place PAND at MMI2 selector `0x12`
  and mask the encoding to the major opcode plus bits 10..0
- PCSX2 `pcsx2/MMI.cpp`, `PAND` and the `tbl_MMI2` dispatch table

## Decision

### 1. Decode only the exact MMI2 selector

Accept primary opcode `0x1C` (already required by the MMI arm), group selector
`0x09` and operation selector `0x12`. The neighboring MMI2 selectors `0x10`
(PMADDH), `0x11` (PHMADH), `0x13` (PXOR), and selector `0x12` in any other MMI
group stay unknown.

### 2. Use a typed IR operation

Add `ParallelAnd { dst, lhs, rhs }`. The decoder reports both source GPRs as
read, the destination as written, no SA dependency, no memory effect, no
synchronous exception and no control transfer. Register effects therefore reuse
the existing parallel-operation arm rather than growing a second policy.

### 3. Form the complete result before assignment

The runtime helper returns `Gpr128{lhs.low & rhs.low, lhs.high & rhs.high}`.
Generated code assigns the whole 128-bit helper result, so a destination that
aliases `rs`, `rt` or both observes the original operands. Writes to
architectural GPR[0] remain suppressed by the generated guard. PAND is
commutative, so the emitted operand order is `(rs, rt)` only to keep the IR
readable; no guest-visible behaviour depends on it, unlike PCPYLD where the
operand order selects the destination halves.

## Validation

- Decoder tests assert the exact MMI2 group and operation selectors, the
  mnemonic, the register fields, the `mmi2` family classification, the
  read/write masks and the absence of delay-slot, SA and exception effects. They
  also assert that selector `0x12` in MMI0 (PEXTLW) and MMI3 (POR) is decoded
  separately, that selector `0x12` in MMI1 and the neighboring MMI2 selectors
  `0x10`, `0x11` and `0x13` stay `Unknown`, and that a zero destination reports
  an empty write mask.
- IR tests assert a distinct typed `ParallelAnd` operation for a non-zero and a
  zero destination, and that the program verifies.
- Codegen tests assert the emitted `parallel_and` call, the `using` declaration,
  the GPR[0] guard and the aliasing call shape.
- A dedicated native fixture (`reps2_mmi2_pand_codegen_smoke`) executes five
  instructions covering a non-aliasing destination, `rd == rs`, `rd == rt`,
  `rd == rs == rt`, and the zero-register destination. The two aliasing cases use
  source values that differ from `r1`/`r2`, so a word that names the wrong source
  register yields a different result instead of matching by accident. The
  fixture asserts hand-derived 128-bit results and that an unrelated register
  keeps both lanes; compile-time assertions check the helper directly, including
  a split-half operand pair that fails if either half is dropped, duplicated or
  swapped, and commutativity of both lanes.
- Mutation evidence, checked manually during review (there is no committed
  harness for this slice, unlike the COP1 mutators under `tools/`). Each mutant
  was built and observed to fail: taking the high half from the low lanes,
  swapping the two halves, and returning `lhs` unchanged are each killed by the
  fixture's static assertions; swapping the lifted operand order is killed by the
  IR and codegen tests; widening the selector mask so `0x13` aliases PAND is
  killed by the decoder test; and using `0x19` instead of `0x09` for the group
  selector is killed by the decoder, IR and codegen tests.

## Consequences

PAND joins POR as a lowered MMI bitwise operation. This is a coverage increment,
not a playability claim: the titles still stop at other unsupported instructions
and unimplemented runtime services. The MMI2 siblings (PNOR, PXOR, PEXTLB,
PEXT5, PPAC5 and the MMI2 multiply/divide group compilers), the other MMI0–MMI3
operations, MMI pipeline-1 state and MMI latency stay out of scope and are still
counted by `profile-elf`.
