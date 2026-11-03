# ADR 0057: EE MMI1 PADDUB unsigned byte saturation

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** R5900 MMI1 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR semantics

## Context

The file-backed census ranked MMI1 `0x28/0x18` (PADDUB) at 358 words. It is a
two-source parallel addition of 16 unsigned byte lanes with unsigned saturation
to 255.

## Architectural evidence

PADDUB is encoded under primary opcode `0x1C` (MMI): low six-bit group
selector `MMI1 = 0x28`, operation selector in bits 10..6 `0x18`, standard
`rs`, `rt`, `rd`.

Semantics: each of the 16 byte lanes adds the corresponding unsigned byte
operands and saturates to 255 on overflow. No exception is raised and no other
architectural state is touched.

## Decision

1. Decode PADDUB under group `0x28` selector `0x18`.
2. Add typed IR operation `ParallelAddUnsignedByte { dst, lhs, rhs }`.
3. Add runtime helper `parallel_add_u8` forming the complete result before
   assignment, with GPR[0] suppression in generated code.

## Validation

- Rust decoder, IR and codegen tests pass.
- Native smoke fixture `reps2_mmi1_paddub_codegen_smoke` executes and exits 0.
- CTest passes.

## Consequences

PADDUB's 358 words are removed from the census. Remaining high-frequency MMI
operations include PEXTLB, PAND, PNOR and others.
