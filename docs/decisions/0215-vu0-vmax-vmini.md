# ADR 0215: VU0 VMAX/VMINI macro broadcast add/min (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-25
- **Scope:** `rust/frontend/src/mips.rs` (`Opcode::Vmaxx/y/z/w`, `Opcode::Vminix/y/z/w`, decode + effects),
  `rust/frontend/src/ir.rs` (`Operation::Vmax`/`Operation::Vmin`, lift),
  `rust/frontend/src/codegen.rs` (emit),
  `runtime/include/reps2/runtime/ee_state.hpp` (`vu0_broadcast_max`/`vu0_broadcast_min`),
  `tests/` (generated-code smoke for VMAX/VMINI), `docs/RESEARCH.md`.
- **Related:** ADR 0192 (VSUB), ADR 0198 (VADDbc broadcast), ADR 0208 (VMULbc broadcast),
  ADR 0209 (VADD), ADR 0212/0213/0214 (frontier abi65).

## Context

ABI-43 probe `abi65-01` (main `8ba2b5b`, runtime ABI 41) advanced past the
`undefined_word_operand` stop at `pc=0x0021B9B8` (ADR 0213) and now stops at
`pc=0x00217D8C` as `unsupported_instruction` on word `0x41010003` (COP2
`fmt >= 0x10`, funct `0x03`): `BC0T` (branch on COP0 condition true). BC0T is
unsolved because the COP0 condition bit is not modeled (ADR 0214, fail-closed).

VU0 macro arithmetic coverage advances: the reachable census on the title
contains 11 VMAX/VMINI words (funct 0x10=3, 0x12=6, 0x13=1, 0x15=1; all other
functs 0), none of which were decoded. VSUB (0x2C), VADD (0x28), VMULbc
(0x18..0x1B), VIADD (0x38), VISUB/VIAND/VIOR (0x39/0x3C/0x3D) are implemented.

## Decision

1. **Decode COP2 Special1 `funct=0x10..0x13` as `Opcode::Vmaxx/y/z/w` and
   `funct=0x14..0x17` as `Opcode::Vminix/y/z/w`.** Following the existing VU
   convention, `x/y/z/w` denotes the *broadcast lane* of `ft`:
   `broadcast_lane = funct & 0x3` (0=x, 1=y, 2=z, 3=w). Operation:
   `fd.lane = max(fs.lane, ft[broadcast_lane])` (VMAX) /
   `fd.lane = min(fs.lane, ft[broadcast_lane])` (VMINI), masked by dest field
   `rs & 0xF` (bit 3=x, 2=y, 1=z, 0=w). GPR[0] write suppressed via `Vf`.
2. **IR:** `Operation::Vmax { dst, fs, ft, broadcast_lane, dest_mask }` and
   `Operation::Vmin { ... }`, same field layout as `VaddBroadcast`/`VmulBroadcast`.
3. **Codegen:** emit
   `vu0_write_vf(state.vu0, dst, vu0_broadcast_max/min(vu0_read_vf(dst),
   vu0_read_vf(fs), vu0_read_vf(ft), broadcast_lane, dest_mask))`.
4. **Runtime:** header-only `constexpr` helpers `vu0_broadcast_max` and
   `vu0_broadcast_min` in `ee_state.hpp`, mirroring `vu0_add_lanes`/`vu0_sub_lanes`
   but reading one scalar lane from `ft` and applying `fpu_max`/`fpu_min` per
   destination lane (Table 8-5 signed-zero ordering through `fpu_max`/`fpu_min`).
   VF0 writes dropped, GPR0 discarded via `Vf`/accessors.
5. **No runtime state, no ABI bump.** `fpu_max`/`fpu_min` already exist
   (ee_fpu.hpp) with EE Table 8-5 semantics.

## Consequences

- VMAX/VMINI words in the frontier census (11 sites in this probe) resolve;
  the probe stop remains `pc=0x00217D8C` BC0T (ADR 0214), not a codegen bug.
- Census effect in `abi65`: none on stop PC; unsupported stops unchanged
  (77) except these 11 now decode.
- No boot stage claimed. Fail-closed: BC0T/BC0F and all other COP2 functions
  remain `Unknown`.

## Validation

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets --
  -D warnings`, `cargo test --workspace --locked` (411 lib tests including
  `decodes_vu0_vmaxx`, `decodes_vu0_vminiw`, `lifts_vu0_vmaxx`,
  `lifts_vu0_vminiw`, `emits_vu0_vmax_and_vmin`).
- CMake Release build + `ctest -C Release`: 52/52 pass on Windows x64/MSVC.
- `tests/cop2_codegen_smoke.cpp` gains `static_assert`s for VMAX/VMINI
  broadcast semantics.
- Real-title probe recorded in `docs/RESEARCH.md` with corrected instruction
  identification (BC0T, not TLBP).
- No boot-tested or playable claim.
