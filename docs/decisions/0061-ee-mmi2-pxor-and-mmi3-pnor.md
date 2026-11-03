# ADR 0061: EE MMI2 PXOR and MMI3 PNOR 128-bit bitwise XOR/NOR

- **Status:** Accepted
- **Date:** 2026-11-04
- **Scope:** R5900 MMI decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR semantics

## Context

The file-backed census over the seven lawfully owned retail ELFs counted the
MMI2/MMI3 bitwise operations alongside the previously lowered PAND and POR.
PXOR and PNOR were implemented in the PAND/POR family but landed on `main`
without a decision record or a dedicated native fixture, so the docs and the
CMake evidence lagged the code. This ADR records the decision and closes that
gap.

## Architectural evidence

Both operations use primary opcode `0x1C` (MMI) with the standard `rs`, `rt`
and `rd` fields and no reserved bits:

- PXOR is MMI2: low six function bits `0x09`, operation selector in bits 10..6
  `0x13`. Selector `0x13` also names MMI0 PPACW and MMI3 PNOR, so the group
  selector is part of the match.
- PNOR is MMI3: low six function bits `0x29`, operation selector in bits 10..6
  `0x13`.

Semantics: PXOR is the plain 128-bit bitwise XOR
`GPR[rd][127:0] = GPR[rs][127:0] ^ GPR[rt][127:0]`; PNOR is the 128-bit bitwise
NOT-OR `GPR[rd][127:0] = ~(GPR[rs][127:0] | GPR[rt][127:0])`. Neither raises an
exception, saturates, or touches state beyond `rs` and `rt`. Writes to `GPR[0]`
are suppressed.

PS2Tek's MMI tables list `PXOR` at MMI2 selector `0x13` and `PNOR` at MMI3
selector `0x13`; QEMU's R5900/TX79 tables mask the encoding to the major opcode
plus bits 10..0; PCSX2's `pcsx2/MMI.cpp` implements both and returns without
storing when `rd` is zero. These are non-normative behavioural references, not
a hardware specification.

## Decision

1. Decode PXOR under MMI2 (function `0x09`) selector `0x13` and PNOR under MMI3
   (function `0x29`) selector `0x13`, verifying the group selector so the shared
   selector is not confused with PPACW, POR or PAND.
2. Add typed IR operations `ParallelXor { dst, lhs, rhs }` and
   `ParallelNor { dst, lhs, rhs }`.
3. Lower them to the runtime helpers `parallel_xor` and `parallel_nor`, which
   form the complete 128-bit result before assignment so source/destination
   aliasing is safe, with GPR[0] suppression in generated code.

## Validation

- Rust decoder, IR and codegen tests cover fields, family classification,
  effects, GPR[0] suppression and neighboring/other-group rejection.
- Native fixtures `reps2_mmi2_pxor_codegen_smoke` and
  `reps2_mmi3_pnor_codegen_smoke` execute normal, `rd == rs`, `rd == rt`,
  `rd == rs == rt`, zero-register and bystander cases with hand-derived vectors
  and compile-time helper assertions.

## Consequences

PXOR and PNOR are now first-class alongside PAND and POR. The remaining
MMI0–MMI3 operations (PEXT5, PPAC5, PADDSW/PSUBSW, the MMI multiplies, the
pipeline-1 state and MMI latency) remain unsupported and are still counted by
`profile-elf`.
