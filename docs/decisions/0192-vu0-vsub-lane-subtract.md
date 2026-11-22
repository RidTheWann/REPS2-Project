# ADR 0192: VU0 VSUB (COP2 Special1 funct=0x2C) lane subtract (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-23
- **Scope:** `rust/frontend/src/mips.rs` (`Opcode::Vsub`, `decode_cop2`,
  effects), `rust/frontend/src/ir.rs` (`Operation::Vsub`),
  `rust/frontend/src/codegen.rs` (emit),
  `runtime/include/reps2/runtime/ee_state.hpp` (`vu0_sub_lanes`),
  `tests/cop2_codegen_smoke.cpp` (compile-time checks), `docs/RESEARCH.md`.
- **Related:** ADR 0154 (COP2 transfers), ADR 0190 (interlock), ADR 0191 (VMR32).

## Context

ABI-40 probe `abi40-01` (main `424e169`) after ADR 0191 advanced past VMR32
and stopped at `pc=0x0023DD64` on `0x4BCC5AEC` as `unsupported_instruction`
after 4,674,770 instructions. The word decodes as COP2 `fmt=0x1E`, `funct=0x2C`
= VSUB, with `fd=11`, `fs=11`, `ft=12`, dest mask `0xE`. The same word dominates
the reachable census (205 sites). It is the first VU0 numeric instruction on
the frontier and the highest-priority remaining family.

Evidence for the operation:

- Sony VU User's Manual §6.2 VSUB: element-wise `fd = fs - ft`, selected by
  the 4-bit dest field (x/y/z/w). §2.1-2.4: single precision, round toward
  zero, no NaN/Inf/denormals; overflow clamps to +-MAX, underflow to +-0.
- PS2Tek COP2 Special1 table row 5 column 4 = VSUB.
- PCSX2 `Int_COP2SPECIAL1PrintTable[0x2C] = VSUB`.
- REPS2 already has a bit-exact EE FPU model (`fpu_sub`) with the same documented
  rules (truncate toward zero, no NaN/Inf/denormal, exponent-based overflow and
  underflow). Reusing that model per lane is the smallest evidenced slice; the
  VU 1-guard-bit rounding quirk is NOT claimed to be bit-exact for all inputs.

## Decision

1. Decode COP2 Special1 `funct=0x2C` as `Opcode::Vsub`; all other Special1
   functions stay `Unknown` and fail closed.
2. IR: `Operation::Vsub { dst: Vf(fd), fs: Vf(fs), ft: Vf(ft), dest_mask }`.
3. Codegen: `vu0_sub_lanes(vu0_read_vf(fs), vu0_read_vf(ft), dest_mask)` written
   through `vu0_write_vf(dst, ...)` so VF0 writes are dropped.
4. Runtime helper `vu0_sub_lanes` applies `fpu_sub` to each of the four 32-bit
   lanes. Dest mask bit 3=x, bit 2=y, bit 1=z, bit 0=w; unselected lanes keep
   the previous destination value. Header-only `constexpr`, no ABI bump.
5. No VU flags, ACC, Q/I/R registers, or interlocks are modelled; only the
   deterministic arithmetic result on VF lanes.

## Consequences

- The ABI-40 stop at `0x0023DD64` should clear on the next probe; the frontier
  advances to the next unsupported VU0 family (VMULx/VADDAx/VADD are next in
  the 620-site census).
- Bit-exactness against hardware is **NOT VERIFIED** for the 1-bit multiply/add
  quirks noted in the VU manual; this ADR claims only the evidenced FPU-model
  equivalence for subtraction with the stated rounding/no-denormal rules.
- No boot stage or Playable claim is made.

## Validation

- `cargo test --package reps2-frontend`: 371 lib tests pass, including new
  `decodes_vu0_vsub_and_effects`, `lifts_vu0_vsub`, `emits_vu0_vsub`.
- `tests/cop2_codegen_smoke.cpp` gained `static_assert`s for full-mask and
  zero-mask VSUB; CTest 51/51 pass.
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked`, frontier unit tests pass.
- Fresh probe (abi41) to be recorded in `docs/RESEARCH.md`.
