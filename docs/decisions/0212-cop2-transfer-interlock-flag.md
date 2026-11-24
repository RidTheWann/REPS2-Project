# ADR 0212: CFC2/CTC2 interlock flag (.I) decode (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Scope:** `rust/frontend/src/mips.rs` (`decode_cop2` transfer arms).
- **Related:** ADR 0190 (QMFC2/QMTC2 `.I`), ADR 0180 (CFC2/CTC2), ADR 0211 (VU Mem0 MMIO).

## Context

ABI-41 probe `abi64-01` (main `a0c1595`, runtime ABI 41) advanced past the VU
Mem0 SQ stop to `pc=0x0021B7E8`, but that block was never translated because its
delay-slot word at `0x0021B844` is `0x48581001`: COP2 `fmt=0x02` (**CFC2**),
`rt=24`, `rd=2`, bit 0 set, bits 10..1 zero. The decoder rejected it as a
reserved-bit violation, so the CFG builder skipped the target block as a
tolerable speculative-decode failure (`skipped_root_count=1`,
`skipped_block_count=1`) and the dispatcher stopped with
`unresolved_control_flow`.

Evidence: ADR 0190 established bit 0 of the COP2 transfer group is the
interlock flag (`.I`), not a reserved bit, for QMFC2/QMTC2. The same rule covers
the whole COP2 transfer family:
- parallel-ps2 / PCSX2 macro-mode emits `_vu0FinishMicro`/`_vu0WaitMicro`
  conditionally on "the interlock bit is known at compile time" for
  QMFC2/CFC2/QMTC2/CTC2;
- PS2SDK `sceCdvdfsv` / EE COP2 encoding treats the transfer group uniformly.

The prior CFC2/CTC2 arms were therefore **inconsistent** with the QMFC2/QMTC2
arms added by ADR 0190: they required bits 10..0 to be zero, rejecting the `.I`
variant that real code uses.

## Decision

1. Decode CFC2 (`fmt=0x02`) and CTC2 (`fmt=0x06`) with the same reserved-field
   mask as QMFC2/QMTC2: bits 10..1 must be zero; bit 0 is the interlock flag
   and is accepted.
2. Decode is unchanged downstream: CFC2/CTC2 semantics (ADR 0180) do not
   depend on the interlock bit; the runtime has no VU micro-program execution,
   so `.I` has no observable effect.
3. Add decode coverage for `0x48581001` (CFC2.I) and `0x48D81001` (CTC2.I),
   plus the negative case (bits 10..1 non-zero stays `Unknown`).
4. No runtime state, no ABI change.

## Consequences

- Frontier should advance past `pc=0x0021B844`.
- The COP2 transfer-family decode rule is uniform again (bit 0 = `.I`).
- No boot/playable claim.

## Validation

- `cargo fmt/clippy/test` (409 lib tests), CTest 52/52.
- Fresh probe (abi65) recorded in `docs/RESEARCH.md`.