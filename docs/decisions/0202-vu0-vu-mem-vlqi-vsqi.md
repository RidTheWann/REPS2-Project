# ADR 0202: VU0 data memory (VU Mem0, 4 KiB) + VLQI/VSQI (COP2 Special2) (runtime ABI 41)

- **Status:** Accepted
- **Date:** 2026-11-23
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp` (`Vu0State::data_mem`,
  `vu0_read_vumem_qword`/`vu0_write_vumem_qword`),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 40 -> 41),
  `rust/frontend/src/mips.rs` (`Opcode::Vsqi`, `Opcode::Vlqi`),
  `rust/frontend/src/ir.rs` (`Operation::Vsqi`, `Operation::Vlqi`),
  `rust/frontend/src/codegen.rs` (emit), `docs/RESEARCH.md`.
- **Related:** ADR 0201 (VI register file, prerequisite for VI addressing),
  ADR 0191 (Special2 decode gate), ADR 0180 (CFC2/CTC2).

## Context

ABI-40 probe `abi53-01` (main `345e8a6`, runtime ABI 40) stopped at
`pc=0x0023CEF8` on word `0x4BE78B7D`: COP2 `fmt=0x1F`, `funct=0x3D` (>= 0x3C,
so Special2), Special2 index `(word & 3) | ((word >> 4) & 0x7C)` = 53,
`rt=7` (ft/VF), `rd=17` (VI base). PCSX2
`Int_COP2SPECIAL2PrintTable[53] = VSQI`; PS2Tek Special2 row 13 column 1.
VSQI is "Store quadword with post-increment": store VF[ft] to VU data memory at
address `VI[rd] * 16`, then `VI[rd] += 1` (in 128-bit word units).

Evidence: Sony VU User's Manual §1.1.5 (VU Mem: 4 KiB for VU0, 128-bit width,
qword-aligned, address divided by 16 is the VI word index) and §6.2 VSQI
(p. 313); PS2Tek Special2 row 13; PCSX2 `microVU_Macro.inl` `recVSQI` ->
`mVU_SQI` lower instruction with `getVUmem(x) = (x & 0xff) * 16` for VU0 and
post-increment `VI[Is] += 1`. The companion load VLQI (index 52) is the same
family and is implemented in the same slice because the stop sequence
`0x23CEF8..0x23CF00` is three VSQI followed by `0x23CF04` = `0x3E00008`
(`jr $ra` delay-slot idiom), and the mirrored code at `0x23CF10..0x23CF24`
repeats the pattern.

REPS2 had no VU0 data memory model, so VLQI/VSQI/VLQD/VSQD could not be
modelled. VU Mem0 is also MMIO-mapped at `0x11004000..0x11004FF0` in EE address
space (Sony VU User's Manual §3.2 memory map), but that MMIO path is **not** part
of this slice; only the COP2 instruction path is modelled.

## Decision

1. Add `std::array<Gpr128, 256> data_mem{}` to `Vu0State` (4 KiB = 256
   quadwords, reset zero). `sizeof(EeState)` changes, so
   `REPS2_RUNTIME_ABI_VERSION` moves 40 -> 41; ABI-40 modules are refused.
2. Runtime helpers:
   - `vu0_read_vumem_qword(const Vu0State&, std::uint8_t vi_index) -> Gpr128`
     returns `data_mem[vi_index & 0xFF]` (VU0 mask 0xFF per PCSX2 `getVUmem`).
   - `vu0_write_vumem_qword(Vu0State&, std::uint8_t vi_index, Gpr128)` stores
     at `data_mem[vi_index & 0xFF]`.
   - VI update: write to VI index 0 is dropped (VI00 constant zero); otherwise
     `vi[index] = static_cast<std::uint16_t>(vi[index] + 1)` for VLQI/VSQI
     (post-increment) and `- 1` for VLQD/VSQD (pre-decrement, not in this
     slice but the helper supports the delta).
3. Decode COP2 Special2 index 52 as `Opcode::Vlqi` and index 53 as
   `Opcode::Vsqi`, gated by `(word & 0x3F) >= 0x3C` (Special2 gate, ADR 0196).
   Fields: `ft = bits 20..16 (rt)` = VF register, `is = bits 15..11 (rd)` = VI
   base register. Dest mask (bits 24..21) is ignored by VLQI/VSQI on real
   hardware (full quadword always transferred); REPS2 stores/loads the full
   quadword.
4. IR: `Operation::Vlqi { dst: Vf, vi: u8 }` and `Operation::Vsqi { src: Vf, vi: u8 }`.
5. Codegen:
   - VLQI: `vu0_write_vf(state.vu0, ft, vu0_read_vumem_qword(state.vu0, state.vu0.vi[is] & 0xFF)); if (is != 0) state.vu0.vi[is] += 1;`
   - VSQI: `vu0_write_vumem_qword(state.vu0, state.vu0.vi[is] & 0xFF, vu0_read_vf(state.vu0, ft)); if (is != 0) state.vu0.vi[is] += 1;`
6. The VI file and VU mem are not part of the VF/GPR effect masks;
   `effects()` records no VF/GPR change for VLQI/VSQI beyond the VLQI VF write.

## Consequences

- Frontier should advance past `0x0023CEF8` (VSQI at `0x23CEF8`, `0x23CEFC`,
  `0x23CF00`).
- VU Mem0 is now available for VLQD/VSQD, VILWR/VISWR, and EE MMIO access at
  `0x11004000` in later slices.
- No boot/playable claim.

## Validation

- Tests `decodes_vu0_vsqi`, `decodes_vu0_vlqi`, `lifts_vu0_vsqi`,
  `lifts_vu0_vlqi`, `emits_vu0_vsqi`, `emits_vu0_vlqi`.
- Generated-code smoke test `reps2_cop2_codegen_smoke` exercises VIADDI
  (ADR 0201), VSQI VF5→VI2++, VLQI VF6→VI3++, and round-trip SQC2 from
  the stored quadword; VI index masking and VI00 suppression verified.
- `cargo fmt/clippy/test` (394 lib tests), CTest 51/51.
- Fresh probe `abi54-01` (main `345e8a6`, runtime ABI 41): frontier advanced
  from `pc=0x0023CEF8` (VSQI) to `pc=0x00221F5C` (**VIADD**, Special1 funct
  0x38), +8,357 instructions (4,708,728 → 4,717,085), census unsupported
  stops 459 → 457. Evidence recorded in `docs/RESEARCH.md`.
