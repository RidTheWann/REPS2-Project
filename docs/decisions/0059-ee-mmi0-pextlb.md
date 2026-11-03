# ADR 0059: EE MMI0 PEXTLB low-byte interleave

- **Status:** Accepted
- **Date:** 2026-11-04
- **Scope:** R5900 MMI0 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR semantics

## Context

The implemented MMI slice covers the word and halfword lane arithmetic, PADDUW and
PADDUB, SA/QFSRV, PCPYLD, PEXEW, PROT3W, POR, PCPYUD, PEXTLW, PPACW, PAND, PXOR and
PNOR.
The next bounded data-movement operation is PEXTLB, which adds no architectural
state, memory access, exception or control-flow behavior.

## Architectural evidence

PEXTLB is encoded in the MMI0 group:

- Primary opcode: `MMI = 0x1C`.
- Low six-bit group selector: `MMI0 = 0x08`.
- Five-bit operation selector in bits 10..6: `PEXTLB = 0x1A`.
- Standard `rs`, `rt` and `rd` fields; no reserved register or immediate field.

Its result is the byte interleave

`[rt0, rs0, rt1, rs1, ..., rt7, rs7]`

from the low eight bytes of each source. Both source high doublewords are ignored.
The complete 128-bit result is formed before assignment; writes to architectural
`GPR[0]` are suppressed.

Primary references:

- Sony *EE Core Instruction Set Manual*, PEXTLB entry
- PS2Tek MMI encoding table
- QEMU R5900/TX79 instruction tables
- PCSX2 `pcsx2/MMI.cpp`, used only as a non-normative behavioral cross-check

## Decision

1. Decode only primary `0x1C`, group selector `0x08`, and operation selector
   `0x1A`. Other groups and neighboring selectors remain `Unknown`.
2. Represent PEXTLB with a typed IR operation carrying `rs`, `rt` and `rd`.
   Effects read both source GPRs, write only a non-zero destination, and report no
   SA, memory, exception or control-flow dependency.
3. Lower through a constexpr runtime helper that interleaves the low eight bytes
   and returns a complete `Gpr128` before the generated destination assignment.

## Validation

- Decoder tests verify `0x70221E88`, exact fields and MMI0 classification, effects,
  zero-destination suppression, neighboring encodings and another MMI group using
  selector `0x1A`.
- IR and codegen tests verify the distinct typed operation, verifier acceptance,
  helper call and `GPR[0]` guard.
- `reps2_mmi0_pextlb_codegen_smoke` compiles generated C++ and executes hand-derived
  vectors for normal operation, ignored high bytes, `rd == rs`, `rd == rt`,
  `rd == rs == rt`, zero-register suppression and an untouched register.
  Compile-time helper assertions cover both result halves and operand order.

## Consequences

PEXTLB joins the lowered MMI data-movement subset. This is a semantic and test
coverage increment, not a title-boot or playability claim. MMI pipeline-1 state,
latency, the remaining MMI operations and the wider PS2 runtime remain out of
scope and are still reported explicitly.
