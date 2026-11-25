# ADR 0214: COP0 condition bit and BC0T/BC0F branches (frontier gate)

- **Status:** Accepted
- **Date:** 2026-11-25
- **Scope:** Documentation only — `docs/decisions/0213-word-shift-operand-scope.md` correction, `docs/RESEARCH.md`, `docs/PROJECT_STATE.md`. No code change.
- **Related:** ADR 0213 (word-shift operand scope), ADR 0022 (COP1 compares/BC1 + FCR31 condition bit), ADR 0025 (COP0 MFC0/MTC0/EI/DI/ERET).

## Context

The `abi65` real-title probe (main `d20afd9`, runtime ABI 41) advanced past the
`undefined_word_operand` stop at `pc=0x0021B9B8` (ADR 0213 fixed the over-strict
`is_word_value` source guard on word shifts) and now stops at
`pc=0x00217D8C` as `unsupported_instruction` on word `0x41010003`.

ADR 0213's validation section misidentified `0x41010003` as COP0 `TLBP`. It is
**not** TLBP. Decode:

```
word   = 0x41010003
primary= 0x10 (COP0)
rs     = 0x08  (COP0 branch family)
rt     = 0x01  (variant 1 = BC0T, branch-on-condition-true)
offset = 0x0003 (signed word offset, ×4 = 12 bytes)
```

TLBP would be word `0x4200'0001` (primary `0x10`, rs `0x10`, funct `0x01`). The
title's ELF contains 32+ BC0T words (`primary 0x10`, rs `0x08`, rt `1`) and
34+ CO words (`rs 0x10`), confirming BC0T is a real, structurally present
instruction family in this title — not a one-off encoding anomaly.

## Decision

1. **Errata ADR 0213:** the `pc=0x00217D8C` stop is BC0T (COP0 branch on
   condition true), not TLBP. ADR 0213's validation text is corrected
   accordingly. The word-shift fix itself remains valid and verified.
2. **BC0T/BC0F decode stays `Opcode::Unknown` (fail-closed).** BC0T branches on
   the **COP0 condition bit**, which REPS2 does not model. COP0 state currently
   covers only register transfers (MFC0/MTC0), `EI`/`DI` (Status.EIE bit 16),
   and `ERET` (ADR 0025). The FCR31 condition bit (ADR 0022) is the *FPU*
   condition consumed by `BC1`, not the COP0 condition consumed by `BC0`.
3. **No COP0 condition bit is inferred or assigned.** There is no evidence in
   this repo for which Status/Cause bit (if any) the EE exposes as the COP0
   condition. Guessing a bit position would violate "Fail Closed" and
   "Evidence Before Assertion".
4. **Next gate:** obtain evidence for the COP0 condition bit (Sony EE manuals
   or an independent cross-check such as PCSX2 used only as a behavioral
   reference — never copied implementation code). Only then may `BC0T`/`BC0F`
   be decoded, lifted, and lowered through the existing CFG branch-condition
   layer (same pattern as ADR 0022 for BC1).

## Consequences

- The `pc=0x00217D8C` stop remains `unsupported_instruction` until the COP0
  condition bit is evidenced and modeled. No boot stage is claimed.
- Decoding BC0T/BC0F as `Unknown` is correct: every writer of `gpr[].low` was
  audited (ADR 0213) and no word producer is implicated here; the stop is a
  genuine missing architectural state model, not a codegen bug.
- Census effect: none yet — BC0T remains an unsupported stop counted in the
  frontier census.

## Validation

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets --
  -D warnings`, `cargo test --workspace --locked` (no code change, 410 lib
  tests pass).
- CMake Release build + `ctest -C Release`: 52/52 pass on Windows x64/MSVC.
- Real-title probe recorded in `docs/RESEARCH.md` with corrected instruction
  identification (BC0T, not TLBP).
- No boot-tested or playable claim.
