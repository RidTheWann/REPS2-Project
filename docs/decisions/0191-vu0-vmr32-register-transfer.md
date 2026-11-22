# ADR 0191: VU0 VMR32 (COP2 Special2 index 49) register transfer (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `rust/frontend/src/mips.rs` (`Opcode::Vmr32`, `decode_cop2`),
  `rust/frontend/src/ir.rs` (`Operation::Vu0RotateRightWord`),
  `rust/frontend/src/codegen.rs` (emit),
  `runtime/include/reps2/runtime/ee_state.hpp`
  (`vu0_rotate_right_word` constexpr helper),
  `tests/cop2_codegen_smoke.cpp` (compile-time rotate check),
  `docs/RESEARCH.md`.
- **Related:** ADR 0154 (COP2 quadword transfers), ADR 0190 (interlock bit).

## Context

ABI-39 probe `abi39-01` (main `a2e3c0f`) after ADR 0190 advanced one
instruction past QMFC2.I and stopped at `pc=0x00221F28` on word `0x4BEB033D`
as `unsupported_instruction`. Decoding: primary `0x12` COP2, `fmt=31`
(Special2), PCSX2 Special2 index `(word & 3) | ((word >> 4) & 0x7C)` = 49,
`rt=11`, `rd=0`, bits 5..2 = 0xF. PCSX2 `Int_COP2SPECIAL2PrintTable[49]` =
`VMR32`, and PS2Tek row 1100 column 1 = VMR32 — confirmed by both tables.

**Primary evidence for the operation:**

- Sony VU User's Manual, MR32 ("Move and rotate per word"): `ft.y <- fs.x`,
  `ft.z <- fs.y`, `ft.w <- fs.z`, `ft.x <- fs.w`. With words `[a,b,c,d]` the
  result is `[d,a,b,c]`.
- PCSX2 `Int_COP2SPECIAL2PrintTable` index 49 = `VMR32`.
- PS2Tek COP2 Special2 row 1100 column 001 = VMR32.

VMR32 is a pure register-to-register move with rotation: no memory, no
exception, no flags, no VU numeric pipeline state beyond the VF file.

## Decision

1. Decode COP2 `fmt=31` Special2 index 49 with bits 5..2 == 0xF as
   `Opcode::Vmr32`; all other Special1/Special2 rows stay `Unknown` and
   fail closed.
2. IR: `Operation::Vu0RotateRightWord { dst: Vf(rt), src: Vf(rd) }`.
3. Codegen: `vu0_write_vf(state.vu0, dst, vu0_rotate_right_word(vu0_read_vf(state.vu0, src)))`.
   VF0 writes are dropped by `vu0_write_vf`, matching the VF0 constant rule.
4. Runtime helper `vu0_rotate_right_word` is a header-only `constexpr`
   function over `Gpr128`, so the runtime ABI is unchanged.
5. No VU numeric model, ACC, flags, or Q/I/R registers are touched.

## Consequences

- The ABI-39 stop at `0x00221F28` should clear on the next probe with a
  rebuilt frontend; the frontier advances to the next unsupported site
  (likely VU0 arithmetic VSUB/VMULx per the 620-site census).
- VMR32 is the only Special2 row implemented; VLQI/VSQI/VLQD/VSQD (VU mem
  load/store via VI) and all Special1 arithmetic remain fail-closed.
- No boot stage or Playable claim is made.

## Validation

- `cargo test --package reps2-frontend` — 368 lib tests pass, including new
  `decodes_vu0_vmr32_register_transfer` (RED before, GREEN after),
  `lifts_vu0_vmr32_rotate`, `emits_vu0_vmr32_rotate`.
- `tests/cop2_codegen_smoke.cpp` gained a `static_assert` pinning the exact
  rotate direction against `vu0_rotate_right_word`.
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` all pass.
- CTest 51/51 pass. Fresh probe (abi40) to be recorded in `docs/RESEARCH.md`.
