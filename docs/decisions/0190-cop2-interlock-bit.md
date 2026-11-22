# ADR 0190: COP2 QMFC2/QMTC2 interlock bit 0 is valid (not reserved)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `rust/frontend/src/mips.rs` (`decode_cop2` reserved-bit check),
  `docs/decisions/0154-vu0-quadword-transfers.md`, `docs/RESEARCH.md`.
- **Related:** ADR 0154 (QMFC2/QMTC2 transfer decode), ABI-38 probe `abi38-01`.

## Context

The ABI-38 probe stopped at `pc=0x00221F24` after 4,673,150 instructions with
`unsupported_instruction` on word `0x48220001`. ADR 0154 classified this as
malformed because REPS2 required COP2 `fmt=1` reserved bits 10..0 to be zero.
That classification was wrong.

Primary/authoritative evidence:

- **PCSX2** `pcsx2/x86/microVU_Macro.inl`, `COP2_Interlock(bool mBitSync)`:
  `if (cpuRegs.code & 1) { ... }` — bit 0 selects interlocked COP2 transfer
  execution. The same pattern applies to COP2 fmt 1/2/5/6 (QMFC2/CFC2/QMTC2/
  CTC2). `recQMFC2()` additionally tests `!(cpuRegs.code & 1)` for the
  non-interlocked path.
- **ps2xRecomp** `include/ps2recomp/instructions.h`:
  `VU0OPS_QMFC2_NI = 0x00`, `VU0OPS_QMFC2_I = 0x01`,
  `VU0OPS_QMTC2_NI = 0x02`, `VU0OPS_QMTC2_I = 0x03` — bit 0 = interlock.
- **libretro/parallel-ps2** (PCSX2-derived): "the interlock bit is known at
  compile time, so the `_vu0FinishMicro`/`_vu0WaitMicro` call is only emitted
  when set" for COP2 transfers.
- **Sony VU User's Manual** §5.4.5/§6: COP2 transfers can interlock with a
  running VU0 micro subroutine; the M-bit of the VU microinstruction releases
  an interlocked `QMTC2.I`/`CTC2.I`. ps2dev discussion corroborates `.I` forms.
- PS2Tek/PS2SDK decoding tables list QMFC2=fmt1, CFC2=fmt2, QMTC2=fmt5,
  CTC2=fmt6; only bits 10..1 are genuinely reserved for the quadword moves.
  CFC2/CTC2 keep bits 10..0 reserved.

**Derived:** `0x48220001` = COP2 fmt=1 (`QMFC2`), `rt=2` (`$v0`), `rd=0`
(VF0), bit 0 = 1 → **QMFC2.I $2, VF0**: valid interlocked quadword move.

## Decision

1. `decode_cop2` accepts bit 0 as the interlock flag for `fmt=1` (QMFC2)
   and `fmt=5` (QMTC2). Bits 10..1 must still be zero.
2. CFC2 (`fmt=2`) and CTC2 (`fmt=6`) keep the full 10..0 reserved check;
   no evidence was found that their bit 0 is an interlock flag.
3. Both QMFC2 and QMFC2.I decode to the existing `Opcode::Qmfc2` and use the
   same data-movement semantics. No timing/interlock stall is modelled:
   REPS2 has no VU0 microprogram execution, so an interlocked transfer is
   observationally identical to the non-interlocked form in this runtime.
4. Update ADR 0154 and RESEARCH.md: `0x48220001` is not malformed; the
   genuinely malformed case is a non-zero bit in 10..1 (e.g. `0x48220002`).
5. Decoder test updated accordingly; no ABI bump (frontend-only decode fix;
   runtime ABI unchanged).

## Consequences

- The ABI-38 stop at `0x00221F24` should be removed on the next probe with
  a rebuilt frontend; the frontier should advance to the next unsupported
  site after this valid QMFC2.I.
- If the title reaches another valid `.I` transfer, it decodes correctly
  without fabricating VU0 microprogram state.
- No boot stage or Playable claim is made.

## Validation

- `cargo test --package reps2-frontend decodes_cop2` passes with the new
  assertions (`0x48220001` -> `Qmfc2`, `0x48220002` -> `Unknown`).
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` all pass.
- Fresh ABI-38/39 probe to be recorded in `docs/RESEARCH.md`.
