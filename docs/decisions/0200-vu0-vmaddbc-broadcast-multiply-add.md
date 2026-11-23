# ADR 0200: VU0 VMADDbc broadcast multiply-add (COP2 Special1 funct 0x08..0x0B) (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-23
- **Scope:** `rust/frontend/src/mips.rs`, `rust/frontend/src/ir.rs`,
  `rust/frontend/src/codegen.rs`, `runtime/include/reps2/runtime/ee_state.hpp`
  (`vu0_broadcast_madd`), `docs/RESEARCH.md`.
- **Related:** ADR 0198 (VADDbc), ADR 0196 (VMADDw), ADR 0193 (ACC).

## Context

ABI-51 probe `abi51-01` stopped at `pc=0x002239FC` on word `0x4BE00448`:
COP2 `fmt=0x1F`, `funct=0x08`, `fd=17`, dest mask `0xF`. PCSX2
`Int_COP2SPECIAL1PrintTable[0x08] = VMADDx`: `fd.dest = ACC + fs * ft.x`
(broadcast x lane). Funct 0x08..0x0B = VMADDx/y/z/w. Sony VU manual §6.2
VMADDbc and PS2Tek Special1 row 1 agree.

Note: VMADDw (`funct=0x0B`) already has a separate IR/codegen path
(`Vu0MaddW`) from ADR 0196; the new `VmaddBroadcast` covers x/y/z with ACC
source preserved.

## Decision

1. Decode Special1 funct 0x08..0x0A as `Vmaddx/Vmaddy/Vmaddz2`; funct 0x0B
   stays on the existing `Vmaddw`/`Vu0MaddW` path.
2. IR `Operation::VmaddBroadcast { dst, fs, ft, broadcast_lane, dest_mask }`.
3. Codegen emits `vu0_write_vf(dst, vu0_broadcast_madd(dst, acc, fs, ft, lane, mask))`.
4. Runtime helper `vu0_broadcast_madd` (header-only `constexpr`) applies
   `fpu_add(acc_lane, fpu_mul(fs_lane, ft_broadcast).bits)` per selected lane;
   unselected lanes preserve the destination value.
5. No EeState change; ABI stays 39.

## Consequences

- Frontier should advance past `0x002239FC`.
- Bit-exactness caveat from ADR 0198 carries over; physical-hardware VU
  rounding quirks are NOT VERIFIED.
- No boot/playable claim.

## Validation

- New decode/effects test for VMADDx; full Rust suite 388 lib tests pass,
  CTest 51/51, fmt/clippy green. Fresh probe (abi52) in `docs/RESEARCH.md`.
