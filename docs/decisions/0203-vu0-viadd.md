# ADR 0203: VU0 VIADD integer register add (COP2 Special1 funct 0x38) (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Scope:** `rust/frontend/src/mips.rs` (`Opcode::Viadd`),
  `rust/frontend/src/ir.rs` (`Operation::Viadd`),
  `rust/frontend/src/codegen.rs` (emit), `docs/RESEARCH.md`.
- **Related:** ADR 0201 (VI register file + VIADDI), ADR 0202 (VU Mem0, and
  the `vu0_read_vi`/`vu0_write_vi` helpers this slice reuses).

## Context

ABI-41 probe `abi54-01` (main `345e8a6`, runtime ABI 41) stopped at
`pc=0x00221F5C` on word `0x4A004BF8`: COP2 `fmt=0x10` (Special1),
`funct=0x38`. PCSX2 `Int_COP2SPECIAL1PrintTable[0x38] = VIADD` (Special1
row 7); PS2Tek COP2 Special1 row 7 column 0 = `VIADD`. The fields are
`it = bits 20..16 (rt)`, `is = bits 15..11 (rd)` and `id = bits 10..6`
(`shift_amount`), the three-operand integer register add
`VI[id] = VI[is] + VI[it]`, evaluated with 16-bit wraparound.

Evidence: Sony VU User's Manual §6.2 `VIADD` (p. 257, "Add Integer"), which
lists it under the macro-mode integer group alongside VIADDI/VIAND/VIOR/
VISUB and documents the macro lower-instruction field layout (`it`/`is`/`id`,
§6.1.2 LowerOP type 1); PS2Tek COP2 Special1 row 7; PCSX2
`microVU_Lower.inl` `mVU_IADD` uses `_Id_`, `_Is_`, `_It_` with
`xADD(regS, regT)` on the 16-bit VI values, and `microVU_Macro.inl`
`REC_COP2_mVU0(IADD, "IADD", 0x104)` routes the macro form to it.

REPS2 decoded nothing at Special1 `funct=0x38`, so this word was an
`unsupported_instruction` stop. The VI register file and its
`vu0_read_vi`/`vu0_write_vi` accessors already exist (ADR 0201/0202), so no
runtime state and no ABI bump are needed.

## Decision

1. Decode COP2 Special1 `funct=0x38` as `Opcode::Viadd` carrying
   `dst`/`is`/`it` from `shift_amount`/`rd`/`rt`. The decoder already accepts
   all `fmt >= 0x10` Special1 rows (ADR 0192 onward), so this is one added
   match arm; no reserved-field change.
2. IR `Operation::Viadd { dst: u8, is: u8, it: u8 }`.
3. Codegen:
   `reps2::runtime::vu0_write_vi(state.vu0, dst, (u16)(vu0_read_vi(state.vu0, is) + vu0_read_vi(state.vu0, it)))`.
   `vu0_write_vi` applies the 5-bit→0xF mask and drops VI00 writes; the
   `std::uint16_t` cast gives the documented 16-bit wraparound.
4. `effects()` records no VF/GPR change: the VI file is not part of those
   masks (same rule as VIADDI, ADR 0201).

## Consequences

- Frontier should advance past `pc=0x00221F5C`.
- Remaining VI integer group: VISUB (`funct=0x39`) and VIAND (`funct=0x3C`)/
  VIOR (`funct=0x3D`) share this shape and are the next slices.
- No boot/playable claim.

## Validation

- Tests `decodes_vu0_viadd`, `lifts_vu0_viadd`, `emits_vu0_viadd` on word
  `0x4A004BF8` (id=15, is=9, it=0).
- `cargo fmt/clippy/test`, CTest. Fresh probe (abi55) recorded in
  `docs/RESEARCH.md`.