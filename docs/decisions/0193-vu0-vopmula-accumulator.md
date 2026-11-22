# ADR 0193: VU0 VOPMULA (COP2 Special1 funct=0x3E) outer product multiply-accumulate (runtime ABI 39)

- **Status:** Accepted
- **Date:** 2026-11-23
- **Scope:** `rust/frontend/src/mips.rs` (`Opcode::Vopmula`, decode, effects),
  `rust/frontend/src/ir.rs` (`Operation::Vopmula`),
  `rust/frontend/src/codegen.rs` (emit),
  `runtime/include/reps2/runtime/ee_state.hpp` (`Vu0State::acc` + `vu0_opmula` helper),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 38 -> 39),
  `docs/RESEARCH.md`.
- **Related:** ADR 0192 (VSUB), ADR 0191 (VMR32), ADR 0154 (COP2 transfers).

## Context

ABI-42 probe `abi42-01` (main `424e169` → `0810ab6`) advanced past VSUB/VMR32
and stopped at `pc=0x0023DD88` on word `0x4BCC5AFE` after 4,674,779
instructions as `unsupported_instruction`. Decoding: primary `0x12` COP2,
`fmt=0x1E` (Special1), `funct=0x3E`, `fd=11`, `fs=11`, `ft=12`, dest mask `0xE`.

**Primary evidence for VOPMULA:**

- Sony VU User's Manual §6.2 VOPMULA: "Outer Product Multiply-Accumulate":
  `ACC.x = ACC.x + fs.x * ft.x; ACC.y = ACC.y + fs.y * ft.y;
   ACC.z = ACC.z + fs.z * ft.z; ACC.w = ACC.w + fs.w * ft.w;`
  Destination is the ACC register, not a VF register. The 4-bit dest field
  is ignored (or used for future extensions).
- PS2Tek COP2 Special1 row 7 column 6 = VOPMULA.
- PCSX2 `Int_COP2SPECIAL1PrintTable[0x3E] = VOPMULA`.
- Census: VOPMULA has 38 reachable unsupported sites (after VSUB 205, VMULx 105,
  VMULAx 81, VADDAx 62, VADD 47).

**Derived:** The guest program is attempting to use the VU0 accumulator for
outer-product multiply-accumulate. This requires the ACC register state
to be modeled, which REPS2 currently does not have (only VF, VI, control
regs). The ACC register is 128-bit, initialized to zero, and updated by
VOPMULA, VADDA, VMULA, VMSUBA, VADDAq, etc.

## Decision

1. Add `Gpr128 acc` field to `Vu0State` (zero-initialized).
2. Decode COP2 Special1 `funct=0x3E` as `Opcode::Vopmula`.
3. IR: `Operation::Vopmula { fs, ft }` (dest is implicit ACC, no fd).
4. Codegen: `state.vu0.acc = vu0_opmula(state.vu0.acc, vu0_read_vf(fs), vu0_read_vf(ft))`.
5. Runtime helper `vu0_opmula` (header-only `constexpr`) computes outer-product
   MAC per lane using the same `fpu_mul` + `fpu_add` model as the EE FPU
   (truncate toward zero, no NaN/Inf/denormal, exponent-based overflow/underflow).
6. `Vu0State` gains `Gpr128 acc{}`, so `sizeof(EeState)` changes and the runtime
   ABI moves 38 -> 39; ABI-38 modules are refused.

## Consequences

- ABI-43 probe should pass VOPMULA and advance further into the VU0 arithmetic
  sequence.
- ACC register state is now tracked but not yet saved/restored on context
  switch (no thread support yet). This is acceptable for single-threaded
  replay.
- No boot stage or Playable claim.

## Validation

- `cargo test --package reps2-frontend`: new tests for `Vopmula` decode/effects,
  lift, codegen.
- `tests/cop2_codegen_smoke.cpp`: static_assert and runtime check for VOPMULA
  on known inputs.
- All gates pass: `cargo fmt/clippy/test`, CTest 51/51, CI green.