# ADR 0195: VU0 VMULAx/VMADDAy/VMADDAz (COP2 Special2) accumulator arithmetic (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-23
- **Scope:** `rust/frontend/src/mips.rs` (`Opcode::Vmulax`, `Vmadday`, `Vmaddaz`),
  `rust/frontend/src/ir.rs`, `rust/frontend/src/codegen.rs`,
  `runtime/include/reps2/runtime/ee_state.hpp`,
  `docs/RESEARCH.md`.
- **Related:** ADR 0193 (Vu0State::acc), ADR 0194 (VOPMSUB).

## Context

ABI-45 probe `abi45-01` (main `d46cfab`) advanced past VOPMULA/VOPMSUB and
stopped at `pc=0x00221950` after 4,675,231 instructions as
`unsupported_instruction` on word `0x4BEE51BC`.

Using the PCSX2 Special2 index formula `(word & 3) | ((word >> 4) & 0x7C)`:

| Word | Index | PCSX2 `Int_COP2SPECIAL2PrintTable` | Meaning |
|---|---|---|---|
| `0x4BEE51BC` | 24 | `VMULAx` | `ACC.x += VF[fs].x * ft.x` |
| `0x4BEE58BD` | 9 | `VMADDAy` | `ACC.y += VF[fs].y * ft.y` |
| `0x4BEE60BE` | 10 | `VMADDAz` | `ACC.z += VF[fs].z * ft.z` |
| `0x4BEE6B8B` | 59 | `VWAITQ` | Q-register sync (no-op in this model) |

The words appear consecutively in a VU0 macro sequence between `LDC2` VF
loads and `SDC2` VF stores. These are the next dominant family in the
reachable unsupported census.

Evidence:

- Sony VU User's Manual §6.2 `VMULAx`, `VMADDAy`, `VMADDAz`:
  multiply-accumulate from a VF register into the ACC register, with `bc`
  broadcast field selecting the source component (`x`/`y`/`z`/`w`).
- PCSX2 `Int_COP2SPECIAL2PrintTable` indexes 24 = VMULAx, 9 = VMADDAy,
  10 = VMADDAz, 59 = VWAITQ.
- PS2Tek COP2 Special2 row 3 (`VMULAx`, `VMULAy`, `VMULAz`, `VMULAw`,
  `VMULAq`, `VABS`, `VMULAi`, `VCLIPw`) and row 1 (`VMADDA*`) agree.

## Decision

1. Decode the four evidenced Special2 indices as distinct opcodes:
   `Vmulax` (24), `Vmadday` (9), `Vmaddaz` (10), `Vwaitq` (59).
2. IR: one `Operation::Vu0AccMulAdd { acc_lane: 0|1|2, fs: Vf, bc: 0|1|2|3 }`
   shared by VMULAx/VMADDAy/VMADDAz; `Vwaitq` is a no-op IR operation.
3. Codegen/runtime: `vu0_acc_mul_add_lane` applies `fpu_add(acc[lane],
   fpu_mul(fs_lane, ft_lane).bits)` using the already evidenced FPU model.
   `VWAITQ` emits no runtime change (REPS2 has no Q pipeline; this is a
   deliberate simplification, `ponytail:` no Q timing until a Q-model ADR).
4. No new EeState field (ACC already present in ADR 0193), so no ABI bump.
5. All other Special2 indices remain `Unknown` and fail closed.

## Consequences

- Next probe should advance past `0x00221950` into the following VU0 macro
  sequence; no boot/playable claim.
- Q-pipeline timing stays unmodelled; `VWAITQ` is a no-op because no
  asynchronous VU execution exists in REPS2. This is honest and bounded.

## Validation

- Decode/effects/lift/codegen tests for the four opcodes.
- `cargo fmt/clippy/test`, CTest 51/51, CI green.