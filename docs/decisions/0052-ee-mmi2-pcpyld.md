# ADR 0052: EE MMI2 PCPYLD low-doubleword copy

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** R5900 MMI2 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR lane semantics

## Context

The current MMI support covers MMI0 PADDW/PSUBW/PADDH/PSUBH, MMI1 PADDUW, and
the MMI SA movers plus QFSRV. A reachable-instruction census across seven
lawfully owned retail ELFs identified PCPYLD as the most frequent remaining
unsupported MMI operation: 851 static reachable words, present in all seven
titles, ahead of POR (554) and PEXTLW (489).

PCPYLD is a pure 128-bit data movement. It introduces no architectural state,
memory access, exception, or control-flow effect, so it is a bounded next unit.

## Architectural evidence

PCPYLD is encoded in the MMI2 group:

- Primary opcode: `MMI = 0x1C`.
- Low six-bit group selector: `MMI2 = 0x09`.
- Five-bit operation selector in bits 10..6: `PCPYLD = 0x0E`.
- Standard `rs`, `rt`, and `rd` fields; no reserved register field.

The operation builds the 128-bit destination from the low doubleword of each
source:

`GPR[rd][63:0] = GPR[rt][63:0]`

`GPR[rd][127:64] = GPR[rs][63:0]`

PCSX2's interpreter implements exactly this assignment and deliberately writes
the high half first so that a destination aliasing either source still observes
the original low doublewords. Its implementation returns immediately when `rd`
is zero, which matches the EE's hard-wired GPR[0].

Primary references:

- Sony *EE Core Instruction Set Manual*, PCPYLD entry
- PCSX2 `pcsx2/MMI.cpp`, `PCPYLD`
- PS2Tek MMI encoding table (MMI2 selector `0x0E`)

## Decision

### 1. Keep MMI2 distinct from other groups

Decode only the exact MMI2 selector `0x09` with operation selector `0x0E`. Do
not alias the same selector in MMI0, MMI1, or MMI3, and do not accept neighboring
MMI2 operation selectors. Preserve the standard register fields and classify the
encoding family as MMI2.

### 2. Use a typed IR operation

Add `ParallelCopyLowDoubleword { dst, lhs, rhs }`. The decoder reports both
source GPRs as read, the destination as written, no SA dependency, no memory
effect, no synchronous exception, and no control transfer.

### 3. Form the complete result before assignment

The runtime helper returns `Gpr128{rhs.low, lhs.low}`. Generated code assigns the
whole 128-bit helper result, so a destination that aliases `rs`, `rt`, or both
sees the original source lanes rather than a partially overwritten register.
Writes to architectural GPR[0] remain suppressed by the generated guard.

## Validation

- Decoder tests assert the exact MMI2 group and operation selectors, mnemonic,
  register fields, family classification, dependency masks, and rejection of the
  same selector in another MMI group and a neighboring MMI2 selector.
- IR tests assert a distinct typed `ParallelCopyLowDoubleword` operation and
  successful verification.
- Codegen tests assert the full 128-bit runtime call, the GPR[0] guard, and the
  aliasing call shape.
- A dedicated native fixture hand-derives the expected low/high halves and
  exercises a non-aliasing destination, `rd == rs`, `rd == rt`, `rd == rs == rt`,
  and the zero-register destination. Static assertions also check the helper
  directly.

## Consequences

The seven-title census no longer counts these 851 PCPYLD words as unsupported.
This is a coverage increment, not a playability claim: the titles still stop at
other unsupported instructions and unimplemented runtime services. MMI pipeline
latency and the remaining MMI0–MMI3 operations stay out of scope.
