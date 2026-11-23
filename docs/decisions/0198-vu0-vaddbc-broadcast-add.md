# ADR 0198: VU0 VADDbc broadcast add (COP2 Special1 funct 0x00..0x03) (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-23
- **Scope:** `rust/frontend/src/mips.rs` (`Opcode::Vaddx/Vaddy/Vaddz/Vaddw`),
  `rust/frontend/src/ir.rs` (`Operation::VaddBroadcast`),
  `rust/frontend/src/codegen.rs` (emit),
  `runtime/include/reps2/runtime/ee_state.hpp` (`vu0_broadcast_add`), `docs/RESEARCH.md`.
- **Related:** ADR 0197 (VMOVE), ADR 0192 (VSUB), ADR 0195 (ACC).

## Context

ABI-49 probe `abi49-01` (main `50db4a2`) advanced past VMOVE and stopped at
`pc=0x002239CC` on word `0x4A500380` after 4,708,697 instructions.

PCSX2 dispatch: primary `0x12`, `fmt=0x12` (Special1), `funct=0x00`, `fd=14`,
`fs=0`, `ft=16`, dest mask `0x2` (z lane). `Int_COP2SPECIAL1PrintTable[0x00]`
= **VADDx**: `fd.dest = fs + ft.x` (broadcast x lane of ft). The four functs
0x00..0x03 are VADDx/VADDy/VADDz/VADDw.

Evidence: Sony VU User's Manual §6.2 VADDbc ("Add broadcast"), PS2Tek Special1
row 0, PCSX2 table.

## Decision

1. Decode Special1 funct 0x00..0x03 as `Opcode::Vaddx/Vaddy/Vaddz/Vaddw`.
2. IR: `Operation::VaddBroadcast { dst, fs, ft, broadcast_lane, dest_mask }`.
3. Codegen: `vu0_write_vf(dst, vu0_broadcast_add(vu0_read_vf(dst),
   vu0_read_vf(fs), vu0_read_vf(ft), lane, mask))`.
4. Runtime helper `vu0_broadcast_add` (header-only `constexpr`): per lane,
   `out = fpu_add(fs_lane, ft_broadcast)` if the dest mask bit is set, else the
   destination lane is preserved. VF0 write dropped by `vu0_write_vf`.
5. No new EeState field; runtime ABI stays 39.

## Consequences

- Next probe should advance past `0x002239CC`.
- Bit-exactness caveat (NOT VERIFIED) carries over from ADR 0192: the VU
  1-guard-bit rounding quirk is not modelled; only the documented
  truncate-toward-zero/no-NaN/no-denormal rules are claimed.
- No boot/playable claim.

## Validation

- New tests: `decodes_vu0_vaddx_and_effects`, `lifts_vu0_vaddx`,
  `emits_vu0_vaddx`.
- `cargo fmt/clippy/test` (383 lib tests), CTest 51/51 pass.