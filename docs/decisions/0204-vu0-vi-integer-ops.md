# ADR 0204: VU0 VISUB / VIAND / VIOR integer register ops (COP2 Special1) (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Scope:** `rust/frontend/src/mips.rs` (`Opcode::Visub`, `Opcode::Viand`,
  `Opcode::Vior`), `rust/frontend/src/ir.rs` (`Operation::Visub`,
  `Operation::Viand`, `Operation::Vior`), `rust/frontend/src/codegen.rs`,
  `docs/RESEARCH.md`.
- **Related:** ADR 0201 (VI file), ADR 0202 (`vu0_read/write_vi`),
  ADR 0203 (VIADD), ADR 0196 (Special2 gate).

## Context

ABI-42 probe `abi55-01` (main `23e3c5e`, runtime ABI 41) advanced the frontier
from `pc=0x00221F5C` (VIADD) to `pc=0x0022FFC8` on word `0x4BCB61BC`
(+81 instructions, 4,717,085 -> 4,716,166; census unsupported stops 457 -> 449).
That word is COP2 `fmt=0x1E` (Special1, 16..30), `funct=0x3C`. PS2Tek COP2
Special1 row `110` lists `VIADD(0x38) VISUB(0x39) VIADDI(0x3A) ---
VIAND(0x3C) VIOR(0x3D)`; PCSX2 `Int_COP2SPECIAL1PrintTable` gives the same
mapping (index 56..61). The two words after the stop are `0x4BCB68BD`
(`funct=0x3D`, VIOR) and `0x4BCB72CA` (a Special1 `VMADDz`), i.e. the title
computes VIAND/VIOR chains.

Key decode rule (ADR 0196): Special2 is `fmt=0x1F` only. `fmt` 16..30 is
always Special1 regardless of funct value, so `fmt=0x1E funct=0x3C/0x3D` is
VIAND/VIOR, not Special2. The VIADD (0x38) and VISUB (0x39) arms are also
`fmt` 16..30 because `fmt=0x1F` routes `funct >= 0x3C` to Special2; the
decoder still needs `fmt != 0x1F` on the 0x3C/0x3D arms so a `fmt=0x1F`
Special2 word is not stolen.

Evidence: Sony VU User's Manual §6.2 `VISUB` (p. 262, "Integer Subtract"),
`VIAND` (p. 259, "Logical Product"), `VIOR` (p. 261, "Logical Sum"); PS2Tek
COP2 Special1 row 7; PCSX2 `microVU_Lower.inl` `mVU_ISUB` (`xSUB`), `mVU_IAND`
(`xAND`), `mVU_IOR` (`xOR`) over the 16-bit VI values.

## Decision

1. Decode COP2 Special1 `funct=0x39` as `Opcode::Visub`, `funct=0x3C` as
   `Opcode::Viand` (fmt != 0x1F), `funct=0x3D` as `Opcode::Vior`
   (fmt != 0x1F). Fields `id = shift_amount`, `is = rd`, `it = rt`.
2. IR `Operation::Visub { dst, is, it }`, `Operation::Viand { .. }`,
   `Operation::Vior { .. }`.
3. Codegen uses the existing `vu0_read_vi`/`vu0_write_vi` helpers with the
   matching operator (`-`, `&`, `|`), cast to `std::uint16_t` for 16-bit
   wraparound. VI00 writes are dropped and the 5-bit operand is masked by the
   helper.
4. `effects()` records no VF/GPR change (the VI file is not in those masks),
   same rule as VIADDI/VIADD.

## Consequences

- Frontier should advance past `pc=0x0022FFC8`.
- No new runtime state, no ABI bump.
- Remaining unmodelled VU integer ops: the Special2 integer group is not
  present in VU0 macro mode; VILWR/VISWR (VI-memory word access) are the
  remaining integer-family holes and are separate slices.
- No boot/playable claim.

## Validation

- Tests `decodes_vu0_visub_viand_vior`, `lifts_vu0_visub_viand_vior`,
  `emits_vu0_visub_viand_vior` on the real stop words `0x4BCB61BC` (VIAND),
  `0x4BCB68BD` (VIOR) and a synthetic VISUB word.
- `cargo fmt/clippy/test` (400 lib tests), CTest 51/51.
- Fresh probe (abi56) recorded in `docs/RESEARCH.md`.