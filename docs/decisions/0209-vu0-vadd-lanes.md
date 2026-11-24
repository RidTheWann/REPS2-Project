# ADR 0209: VU0 VADD vector add (Special1 funct 0x28) (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp` (`vu0_add_lanes`),
  `rust/frontend/src/mips.rs` (`Opcode::Vadd`),
  `rust/frontend/src/ir.rs` (`Operation::VaddBroadcast`),
  `rust/frontend/src/codegen.rs`, `docs/RESEARCH.md`.
- **Related:** ADR 0198 (VADDbc, same masked add lane logic), ADR 0208 (VMULbc).

## Context

abi61-01 probe (runtime ABI 41) stopped at `pc=0x0022E438` on word `0x4BEC5AD8`,
which was first misidentified as VILWR (Special2 index 44) but is correctly
**COP2 Special1 funct=0x28 = VADD** (Special1 row 5 column 0 = `VADD`),
PS2Tek `Int_COP2SPECIAL1PrintTable[0x28]`. Fields: `dst=sa=11` (fd), `fs=rd=11`,
`ft=rt=12`, `dest_mask=0xF`. VADD computes `VF[fd].lane = fs.lane + ft.lane`
per lane (masked by dest mask), using the same VU single-precision add model as
VADDbc (ADR 0198).

Evidence:
- Sony VU User's Manual §6.2 `VADD` (p. 241, "Add"): `VF[fd] = VF[fs] + VF[ft]` per lane.
- PCSX2 `Int_COP2SPECIAL1PrintTable[0x28] = VADD`.
- REPS2 already implements `vu0_sub_lanes` (ADR 0192) — VADD is the same lane
  structure with `fpu_add` instead of `fpu_sub`.

## Decision

1. Add runtime helper `vu0_add_lanes(dst, fs, ft, dest_mask)` mirroring
   `vu0_sub_lanes` but using `fpu_add`.
2. Decode COP2 Special1 `funct=0x28` as `Opcode::Vadd` carrying `dst=sa`,
   `fs=rd`, `ft=rt`, `dest_mask=fmt`.
3. IR `Operation::VaddBroadcast { dst: Vf, fs: Vf, ft: Vf,
   broadcast_lane: u8, dest_mask: u8 }` — same IR shape as VADDbc (ADR 0200),
   distinguished by opcode name only.
4. Codegen calls `vu0_add_lanes(vu0_read_vf(dst), vu0_read_vf(fs),
   vu0_read_vf(ft), dest_mask)`.
5. `effects()` records `vf_read_mask = fs|ft`, `vf_write_mask = dst` (same as
   VADDbc).
6. No runtime state or ABI change.

## Consequences

- Frontier should advance past `pc=0x0022E438` (VMULx → VADD).
- Remaining VU0 macro arithmetic: VSUB (fmt 0x2C), VADD (done), VMULbc (done),
  VMADDx etc. stay fail-closed until evidenced.
- No boot/playable claim.

## Validation

- Tests `decodes_vu0_vadd`, `lifts_vu0_vadd`, `emits_vu0_vadd`.
- `cargo fmt/clippy/test`, CTest 51/51.
- Fresh probe (abi63) recorded in `docs/RESEARCH.md`.