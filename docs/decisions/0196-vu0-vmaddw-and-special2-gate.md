# ADR 0196: VU0 VMADDw + COP2 Special2 funct gate correction (ADR 0195 fix)

- **Status:** Accepted
- **Date:** 2026-11-23
- **Scope:** `rust/frontend/src/mips.rs` (`Opcode::Vmaddw`, `decode_cop2` gate),
  `rust/frontend/src/ir.rs` (`Operation::Vu0MaddW`),
  `rust/frontend/src/codegen.rs` (emit), `docs/decisions/0195-*.md` (correction),
  `docs/RESEARCH.md`.
- **Related:** ADR 0195 (Special2 ACC ops), ADR 0193 (ACC).

## Context

ABI-46 probe `abi46-01` (main `2655969`) advanced past the Special2 ACC ops and
stopped at `pc=0x0022196C` on word `0x4BEF6BCB`. Decoding that sequence with the
authoritative PCSX2 two-level dispatch exposed a decoder bug introduced by
ADR 0195:

- COP2 `fmt=0x1F` (fmt field = bits 25..21) selects the **Special1** table
  (`Int_COP2SPECIAL1PrintTable[funct]`).
- Only when `funct >= 0x3C` does the Special1 table entry redirect to the
  **Special2** table, indexed as `(word & 3) | ((word >> 4) & 0x7C)`.

ADR 0195 decoded every `fmt=0x1F` word through the Special2 index formula
without the `funct >= 0x3C` gate, so `0x4BEE6B8B` (funct=0x0B) was mis-decoded
as `vwaitq` when it is **VMADDw** (Special1 funct 0x0B). The mis-decode was
masked in tests because ADR 0195 only asserted the words it happened to model.

**Authoritative decoding of the frontier sequence (PCSX2 tables):**

| Word | funct | table | name |
|---|---|---|---|
| `0x4BEE51BC` | 0x3C | S2 idx 24 | VMULAx |
| `0x4BEE58BD` | 0x3D | S2 idx 9 | VMADDAy |
| `0x4BEE60BE` | 0x3E | S2 idx 10 | VMADDAz |
| `0x4BEE6B8B` | 0x0B | S1 0x0B | **VMADDw** |
| `0x4BEF6BCB` | 0x0B | S1 0x0B | **VMADDw** |

This is the standard ps2sdk `vuhw.c` matrix-vector multiply:
`vmulax / vmadday / vmaddaz` accumulate into ACC, `vmaddw` writes the result to
`VF[fd]` with `ft.w` broadcast.

## Decision

1. Gate the Special2 decode arms in `decode_cop2` with `(word & 0x3F) >= 0x3C`;
   lower funct values stay in Special1.
2. Add `Opcode::Vmaddw` (Special1 funct 0x0B): `VF[fd] = ACC + fs * ft.w`.
   IR `Operation::Vu0MaddW { dst, fs, ft }`; codegen reuses
   `vu0_acc_broadcast_mul(acc, fs, ft, broadcast_lane=3, accumulate=true)`
   written through `vu0_write_vf(dst, ...)`.
3. Correct ADR 0195: `0x4BEE6B8B`/`0x4BEF6BCB` are VMADDw, not VWAITQ. The
   Special2 VWAITQ (idx 59) remains modelled as a no-op for genuine
   `funct >= 0x3C` words only.
4. No new EeState field; runtime ABI stays 39.

## Consequences

- Frontier should advance past `0x0022196C` on the next probe (abi47).
- The decoder no longer over-claims Special2 for funct<0x3C words; peer
  Special1 arithmetic (e.g. VADD/VMUL) still fails closed unless separately
  evidenced.
- No boot/playable claim.

## Validation

- New tests `decodes_vu0_vmaddw_and_effects` (asserts both `0x4BEE6B8B` and
  `0x4BEF6BCB` decode as `Vmaddw`), `lifts_vu0_vmaddw`, `emits_vu0_vmaddw`.
- `cargo test --workspace --locked` (380 lib tests), `cargo fmt/clippy`, CTest
  51/51 all pass.
- Fresh probe (abi47) to be recorded in `docs/RESEARCH.md`.