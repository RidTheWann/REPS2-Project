# ADR 0197: VU0 VMOVE (COP2 Special2 index 48) masked VF move (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-23
- **Scope:** `rust/frontend/src/mips.rs` (`Opcode::Vmove`, decode, effects),
  `rust/frontend/src/ir.rs` (`Operation::Vmove`),
  `rust/frontend/src/codegen.rs` (emit),
  `runtime/include/reps2/runtime/ee_state.hpp` (`vu0_move_masked`), `docs/RESEARCH.md`.
- **Related:** ADR 0195/0196 (Special2 ACC ops + gate), ADR 0193 (ACC).

## Context

ABI-48 probe `abi48-01` (main `f3d9d83`) advanced past the VMADDw fix and
stopped at `pc=0x002239C8` on word `0x4BF4733C` after 4,708,696 instructions
as `unsupported_instruction`. Decoding with the PCSX2 two-level dispatch:
primary `0x12` COP2, `fmt=0x1F`, `funct=0x3C` (>= 0x3C so Special2), index
`(word & 3) | ((word >> 4) & 0x7C)` = 48 = **VMOVE**, `fs=14`, `ft=20`,
dest mask `0xF`. The word sits in a `LDC2`/`SDC2` vector-copy sequence.

Evidence:

- Sony VU User's Manual §6.2 VMOVE: masked VF-to-VF move, `ft.dest <- fs`.
- PS2Tek COP2 Special2 row 6 column 0 = VMOVE.
- PCSX2 `Int_COP2SPECIAL2PrintTable[48] = VMOVE`.

## Decision

1. Decode COP2 Special2 index 48 as `Opcode::Vmove`; other Special2 indices
   stay `Unknown` unless previously evidenced.
2. IR: `Operation::Vmove { dst: Vf(rt), src: Vf(rd), dest_mask }`.
3. Codegen: `vu0_write_vf(dst, vu0_move_masked(vu0_read_vf(dst), vu0_read_vf(src), mask))`.
4. Runtime helper `vu0_move_masked` (header-only `constexpr`): dest mask bits
   3=x, 2=y, 1=z, 0=w; unselected lanes keep the destination value. VF0 writes
   are dropped by `vu0_write_vf`.
5. No new EeState field; runtime ABI stays 39.

## Consequences

- Next probe should advance past `0x002239C8`.
- VMOVE is exact data movement; no numeric model, no ACC, no flags.
- No boot/playable claim.

## Validation

- New tests: decode/effects, lift, codegen for VMOVE.
- `cargo fmt/clippy/test`, CTest 51/51 pass. Fresh probe (abi49) recorded in
  `docs/RESEARCH.md`.
