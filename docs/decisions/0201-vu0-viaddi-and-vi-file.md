# ADR 0201: VU0 VI integer register file + VIADDI (runtime ABI 40)

- **Status:** Accepted
- **Date:** 2026-11-23
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp` (`Vu0State::vi`),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 39 -> 40),
  `rust/frontend/src/mips.rs` (`Opcode::Viaddi`),
  `rust/frontend/src/ir.rs` (`Operation::Viaddi`),
  `rust/frontend/src/codegen.rs` (emit), `docs/RESEARCH.md`.
- **Related:** ADR 0200 (VMADDbc), ADR 0193 (ACC).

## Context

ABI-52 probe `abi52-01` stopped at `pc=0x0023CEF0` on word `0x4A0703F2`:
COP2 `fmt=0x10` (Special1), `funct=0x32`. PCSX2
`Int_COP2SPECIAL1PrintTable[0x32] = VIADDI`: integer add-immediate on the VU
integer register file, `VI[it] = VI[is] + sign_ext(imm5)`. Evidence: Sony VU
User's Manual §6.2 VIADDI; PS2Tek Special1 row 6 column 2.

REPS2 had no VU integer register file (`VI00..VI15`, 16-bit, VI00 constant
zero), so VIADDI could not be modelled. This is the prerequisite for the whole
VU integer group (VIADD/VISUB/VIAND/VIOR) and for VISWR/VILWR addressing.

## Decision

1. Add `std::array<std::uint16_t, 16> vi{}` to `Vu0State` (reset zero; VI00 is
   constant zero, so writes to index 0 are not performed).
2. `sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 39 -> 40;
   ABI-39 modules are refused.
3. Decode Special1 `funct=0x32` as `Opcode::Viaddi` with fields
   `it = bits 20..16 (rt)`, `is = bits 15..11 (rd)`, `imm5 = bits 10..6`.
4. IR `Operation::Viaddi { dst, src, immediate }` (immediate sign-extended
   from 5 bits).
5. Codegen: `state.vu0.vi[dst] = (u16)(state.vu0.vi[src] + (i16)imm)`; a write
   to VI0 is dropped.
6. The VI file is not part of the VF/GPR effect masks; `effects()` records no
   VF/GPR change for VIADDI.

## Consequences

- Frontier should advance past `0x0023CEF0`.
- The VI file is now available for the remaining integer group (VIADD/VISUB/
   VIAND/VIOR) and for VISWR/VILWR/VLQI/VSQI addressing in later slices.
- Exact 16-bit wrapping is modelled (`static_cast<std::uint16_t>`); VU manual
   §2.1.2 fixed-point rules apply and are honoured by simple truncation.
- No boot/playable claim.

## Validation

- Tests `decodes_vu0_viaddi`, `lifts_vu0_viaddi`, `emits_vu0_viaddi`.
- `cargo fmt/clippy/test` (389 lib tests), CTest 51/51 pass. Fresh probe
  (abi53) recorded in `docs/RESEARCH.md`.
