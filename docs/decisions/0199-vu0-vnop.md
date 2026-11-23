# ADR 0199: VU0 VNOP (COP2 Special1 funct=0x3F) decodes as Nop (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-23
- **Scope:** `rust/frontend/src/mips.rs` (decode_cop2 Special1 arm), `docs/RESEARCH.md`.
- **Related:** ADR 0198 (VADDbc), ADR 0195/0196 (Special2 gate).

## Context

ABI-50 probe `abi50-01` (main `1f2e31e`) stopped at `pc=0x002239DC` on word
`0x4A0002FF`. The word decodes as primary `0x12` COP2, `fmt=0x10`,
`funct=0x3F`, which PCSX2 `Int_COP2SPECIAL1PrintTable[0x3F]` names `VNOP`:
a no-operation. REPS2 had no arm for this funct and stopped with
`unsupported_instruction`, blocking progress past an explicit no-op.

Note: `funct=0x3F` belongs to Special1 even though the Special2 VNOP index is
also 47; the two-level dispatch sends `funct >= 0x3C` through SPECIAL2 only
for the four SPECIAL1 table entries that redirect to it (0x3C..0x3F themselves
are individual SPECIAL1 entries in PCSX2's table). Here `0x3F` is directly
VNOP.

## Decision

1. Decode COP2 Special1 `funct=0x3F` as the existing `Opcode::Nop`.
2. No IR/runtime/codegen change: a Nop is already a no-op.
3. No ABI bump.

## Consequences

- The frontier should advance past `0x002239DC` on the next probe.
- No fabricated state or completion; Nop is genuinely a no-op per PCSX2.

## Validation

- New test `decodes_vu0_vnop_as_nop` (RED before, GREEN after).
- `cargo fmt/clippy/test`, CTest 51/51 pass. Fresh probe (abi51) in `docs/RESEARCH.md`.
