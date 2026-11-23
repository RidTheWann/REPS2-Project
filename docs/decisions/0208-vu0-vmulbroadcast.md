# ADR 0208: VU0 VMULx/VMULy/VMULz/VMULw broadcast multiply (Special1 funct 0x18..0x1B) (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp` (`vu0_broadcast_mul`),
  `rust/frontend/src/mips.rs` (`Opcode::Vmulx`..`Vmulw`),
  `rust/frontend/src/ir.rs` (`Operation::VmulBroadcast`),
  `rust/frontend/src/codegen.rs`, `docs/RESEARCH.md`.
- **Related:** ADR 0198 (VADDbc, same shape), ADR 0207 (frontier misidentification corrected).

## Context

abi61-01 stopped at `pc=0x0022E438` on word `0x4BEC5AD8`, which was
misidentified as VILWR (Special2 index 44) in ADR 0207's context. Correct decode:
`fmt=0x1F` (Special1 family, 16..30), `funct=0x18` = binary `011000`; since bits
5..2 are `0110` (not `1111`), this is **Special1**, row 3 column 0 =
**VMULx** (broadcast multiply by VF[ft].x). PS2Tek COP2 Special1 row 3:
`VMULx(0x18) VMULy(0x19) VMULz(0x1A) VMULw(0x1B)`. PCSX2
`Int_COP2SPECIAL1PrintTable[0x18..0x1B]` matches.

The word's fields: `dest mask = 0xF`, `rt=12` (ft), `rd=11` (fs), `sa=11` (fd).
VMULx computes `fd.dest = fs * ft.x` with VU single-precision multiply rules
(the same `fpu_mul` model used by VADDbc/VMADDbc, ADRs 0198/0200).

## Decision

1. Decode COP2 Special1 `funct=0x18..0x1B` as `Opcode::Vmulx`/`Vmuly`/
   `Vmulz`/`Vmulw` carrying `dst=sa`, `fs=rd`, `ft=rt`, `dest_mask=fmt`.
2. IR `Operation::VmulBroadcast { dst: Vf, fs: Vf, ft: Vf, broadcast_lane: u8, dest_mask: u8 }`.
3. Runtime `vu0_broadcast_mul(dst, fs, ft, lane, dest_mask)` mirrors
   `vu0_broadcast_add` (ADR 0198) with `fpu_mul` in place of `fpu_add`,
   preserving unselected lanes.
4. Codegen writes `vu0_write_vf(dst, vu0_broadcast_mul(vu0_read_vf(dst), vu0_read_vf(fs), vu0_read_vf(ft), lane, mask))`.
5. `effects()` records `vf_read_mask = fs|ft`, `vf_write_mask = dst` (same as VADDbc).
6. No runtime state or ABI change. The misidentified ADR 0207 remains valid
   as an ISA slice (VILWR/VISWR are real instructions) but was not this frontier.

## Consequences

- Frontier should advance past `pc=0x0022E438`.
- VMULq (funct 0x1C) and VMULi (0x1E) stay fail-closed.
- No boot/playable claim.

## Validation

- Tests `decodes_vu0_vmulbroadcast`, `lifts_vu0_vmulbroadcast`,
  `emits_vu0_vmulbroadcast` on word `0x4BEC5AD8` (lane x, mask 0xF).
- `cargo fmt/clippy/test`, CTest 51/51.
- Fresh probe (abi62) recorded in `docs/RESEARCH.md`.