# ADR 0050: EE MMI0 PADDH/PSUBH modulo lane arithmetic

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** R5900 MMI0 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR lane semantics

## Context

ADR 0049 added the basic MMI0 word-lane operations PADDW and PSUBW. The next natural pair in the same MMI0 function table is PADDH / PSUBH (parallel halfword add/subtract). These are widely used for 128-bit SIMD integer arithmetic and require no new architectural state or exceptions.

## Architectural evidence

Sony's *EE Core Instruction Set Manual* documents PADDH and PSUBH under the MMI0 class. The instruction encoding is:

- Primary opcode: `MMI = 0x1C`.
- Low six-bit group selector: `MMI0 = 0x08`.
- Five-bit function field in bits 10..6: `PADDH = 0x04`, `PSUBH = 0x05`.
- Standard `rs`, `rt`, and `rd` fields.

The operation splits each 128-bit source GPR into eight 16-bit halfwords and adds or subtracts corresponding halfwords. The result retains only bits 15..0 of each lane result. There is no carry or borrow across lane boundaries and no exception is defined.

Primary references:

- Sony EE Core Instruction Set Manual (PADDH / PSUBH entries)
- QEMU / PCSX2 opcode tables confirming MMI0 function codes 0x04 / 0x05
- wasaylor/r5900-opcodes encoding table

## Decision

### 1. Keep MMI0 distinct from other groups

Decode only the exact MMI0 selector `0x08` with function field `4` or `5`. Do not alias MMI1/MMI2/MMI3 selectors or reserved function values. Preserve the standard register fields and classify the encoding family as MMI0.

### 2. Use explicit typed IR operations

Add distinct `ParallelAddHalfword` and `ParallelSubHalfword` operations. Each records a validated destination, left source, and right source GPR. Decoder effects read both sources, write the destination, and report no synchronous exception or control transfer.

### 3. Define lane behavior without signed overflow

Treat each 16-bit halfword as an unsigned bit-vector for the host implementation. For lane (i in {0..7}):

`PADDH: dst[i] = (lhs[i] + rhs[i]) mod 2^16`

`PSUBH: dst[i] = (lhs[i] - rhs[i]) mod 2^16`

All eight lanes are calculated independently and packed into the complete 128-bit destination GPR. This makes wraparound defined in C++ and avoids signed-overflow undefined behavior. Neither operation saturates.

### 4. Preserve GPR destination rules

Generated code assigns the complete 128-bit helper result, preserving correct behavior when `rd` aliases either source because the helper returns a value before assignment. Writes to architectural GPR[0] remain suppressed by the generated destination guard.

## Validation

- Decoder tests assert exact MMI0 selector/function matching, mnemonic, register fields, family classification, dependency masks, and rejection of reserved or neighboring MMI-group encodings.
- IR tests assert separate typed PADDH and PSUBH operations.
- Codegen tests assert the correct modulo-lane runtime helper calls and zero-register guard.
- Runtime helpers implement eight independent 16-bit modulo lanes with no cross-lane carry/borrow.
- The same native-fixture and CTest patterns used for PADDW/PSUBW apply; the existing `reps2_mmi0_word_codegen_smoke` style is extended by the halfword helpers.

This completes the non-saturating parallel add/sub pair for the two most common MMI0 lane widths (word and halfword).
