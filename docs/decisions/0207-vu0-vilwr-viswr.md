# ADR 0207: VU0 VILWR/VISWR integer word memory access (COP2 Special2) (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp` (`vu0_read_vumem_word`/`vu0_write_vumem_word`),
  `rust/frontend/src/mips.rs` (`Opcode::Vilwr`, `Opcode::Viswr`),
  `rust/frontend/src/ir.rs` (`Operation::Vilwr`, `Operation::Viswr`),
  `rust/frontend/src/codegen.rs` (emit), `docs/RESEARCH.md`.
- **Related:** ADR 0201 (VI file), ADR 0202 (VU Mem0), ADR 0204 (VISUB/VIAND/VIOR).

## Context

ABI-42 probe `abi60-01` (main `b20a29c`, runtime ABI 41) advanced the frontier from
`pc=0x25A724` (GETERROR) to `pc=0x22E438` (+300K instructions, census unsupported
364 -> 360) on word `0x4BEC5AD8`: COP2 `fmt=0x1F` (Special2), `funct=0x18` (24),
Special2 index `(word & 3) | ((word >> 4) & 0x7C)` = 44, `rt=12` (it), `rd=11` (is).
PS2Tek COP2 Special2 row 11 column 0: **VILWR** — load 16-bit word from VU Mem0 at
address `VI[is]` into `VI[it]`. PCSX2 `Int_COP2SPECIAL2PrintTable[44] = VILWR`.
VISWR (index 45) is the mirror store. Both are in the same instruction stream at
`0x22E438`/`0x22E43C` (`0x4BEC5AD8` / `0x4BEC5BD8` likely), so implement together.

Evidence:
- Sony VU User's Manual §6.2 `VILWR` (p. 260, "Integer Load Word Register") and
  `VISWR` (p. 263, "Integer Store Word Register").
- PS2Tek COP2 Special2 row 11 (columns 0-1): VILWR/VISWR.
- PCSX2 `microVU_Lower.inl` `mVU_ILWR`/`mVU_ISWR` lower instructions: load/store
  16-bit word from/to VU data memory at `VI[is]` byte offset, no post-increment,
  VI operand masked to 0xF (only VI00..VI15 accessible).

REPS2 already models `Vu0State::data_mem` (ADR 0202) and `vu0_read/write_vi` (ADR 0201),
so no new runtime state or ABI change are needed.

## Decision

1. Decode COP2 Special2 index 44 as `Opcode::Vilwr` and index 45 as `Opcode::Viswr`,
   gated by `(word & 0x3F) >= 0x3C` (Special2 gate, ADR 0196). Fields: `it = bits
   20..16 (rt)` = VI dest/src, `is = bits 15..11 (rd)` = VI base register.
2. Runtime helpers:
   - `vu0_read_vumem_word(const Vu0State&, std::uint16_t vi_index) -> std::uint16_t`
     returns `data_mem[vi_index & 0xFF] & 0xFFFF` (16-bit word at byte address
     `vi_index & 0xFF` within the 4 KiB VU0 window).
   - `vu0_write_vumem_word(Vu0State&, std::uint16_t vi_index, std::uint16_t value)`
     stores the 16-bit word at `data_mem[vi_index & 0xFF]`.
   - VI00 write dropped via `vu0_write_vi`; VI00 read returns 0.
3. IR: `Operation::Vilwr { dst: u8, vi: u8 }` and `Operation::Viswr { src: u8, vi: u8 }`.
4. Codegen:
   - VILWR: `vu0_write_vi(state.vu0, dst, vu0_read_vumem_word(state.vu0, vu0_read_vi(state.vu0, vi)));`
   - VISWR: `vu0_write_vumem_word(state.vu0, vu0_read_vi(state.vu0, vi), vu0_read_vi(state.vu0, src));`
5. `effects()` records no VF/GPR change (VI file not in masks), same as VIADDI.

## Consequences

- Frontier should advance past `pc=0x22E438` (VILWR).
- VSQD/VLQD (pre-decrement variants) and VU Mem0 EE MMIO at `0x11004000` are later slices.
- No boot/playable claim.

## Validation

- Tests `decodes_vu0_vilwr_viswr`, `lifts_vu0_vilwr_viswr`, `emits_vu0_vilwr_viswr`.
- `cargo fmt/clippy/test`, CTest 51/51.
- Fresh probe (abi61) recorded in `docs/RESEARCH.md`.