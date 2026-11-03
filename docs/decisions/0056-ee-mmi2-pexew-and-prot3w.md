# ADR 0056: EE MMI2 PEXEW and PROT3W single-source word permutations

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** R5900 MMI2 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR semantics

## Context

The file-backed census ranked MMI2 `0x09/0x1E` (PEXEW, 356 words) and
`0x09/0x1F` (PROT3W, 648 words) as high-frequency remaining operations. Both
are single-source word permutations: they read only `rt`, write only `rd`, and
reserve `rs == 0`. They therefore need their own decode constraint and IR shape
rather than the two-source parallel-operation policy.

## Architectural evidence

Both operations are encoded under primary opcode `0x1C` (MMI):

- PEXEW: low six-bit group selector `MMI2 = 0x09`, five-bit operation
  selector in bits 10..6 `0x1E`, `rs` reserved zero, standard `rt` and `rd`.
- PROT3W: low six-bit group selector `MMI2 = 0x09`, five-bit operation
  selector in bits 10..6 `0x1F`, `rs` reserved zero, standard `rt` and `rd`.

Semantics, agreed across QEMU `target/mips/tcg/tx79_translate.c`
(`trans_PEXEW`, `trans_PROT3W`), PCSX2 `pcsx2/MMI.cpp` (`PEXEW`, `PROT3W`)
and the PCSX2 recompiler:

- PEXEW exchanges the low word of each 64-bit half with the low word of the
  other half. Result words are `[rt1, rt2, rt3, rt0]`.
- PROT3W rotates the four words left by one position. Result words are
  `[rt1, rt2, rt0, rt3]`.

Neither operation saturates, raises an exception or touches any architectural
state other than the destination GPR.

## Decision

1. Decode PEXEW and PROT3W only under group `0x09` selectors `0x1E` and
   `0x1F` with `rs == 0`. A nonzero `rs` remains `Unknown`.
2. Add typed IR operations `ParallelExchangeEvenWord { dst, src }` and
   `ParallelRotate3Words { dst, src }`.
3. Add runtime helpers `parallel_exchange_even_word` and
   `parallel_rotate_3_words`, forming the complete result before assignment so
   an aliasing destination observes the original source.

## Validation

- Decoder tests assert exact selectors, mnemonics, register fields, reserved-rs
  rejection, read/write masks and unknown neighbors.
- IR tests assert typed operations and verification.
- Codegen tests assert single-source helper calls and guards.
- Native smoke fixtures (`reps2_mmi2_pexew_codegen_smoke`,
  `reps2_mmi2_prot3w_codegen_smoke`) execute and exit 0.
- CTest 24/24 pass.

## Consequences

These two units remove 1,004 words from the census. Remaining high-frequency
MMI operations include PADDUB, PEXTLB, PAND, PNOR and others.
