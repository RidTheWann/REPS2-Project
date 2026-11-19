# ADR 0162: Decode, lift and AOT-lower R5900 MMI0 PCGTW

- **Status:** Accepted (validated locally; no real-title probe).
- **Date:** 2026-11-17.
- **Scope:** MMI0 decode, typed IR, generated C++ lowering, one `Gpr128`
  comparison helper, and a synthetic generated-code fixture. No runtime ABI,
  engine state, or title profile changes.

## Context

The PS2TeK MMI0 table identifies PCGTW at function `0x08`, selector `0x02`.
PCSX2's `PCGTW` implementation at revision
`79929f01750fdc8185810968c934bd3923d099a7` compares four signed 32-bit lanes
and emits all ones or zero per lane. These sources are cross-checks, not a
hardware trace or normative specification; their references and test details
are recorded in `docs/RESEARCH.md`.

## Decision

1. Decode PCGTW only under primary opcode `0x1C`, function `0x08`, and selector
   bits 10:6 equal to `0x02`. Other selectors and MMI groups remain distinct.
2. Model the operation as a two-source, one-destination typed IR instruction.
   Effects read both source GPRs and write the destination unless it is GPR[0].
3. Implement a `constexpr` helper that compares four independent signed
   32-bit lanes. `std::bit_cast<std::int32_t>` gives the signed interpretation
   of each raw lane without relying on implementation-defined conversions.
   Each true comparison produces `0xFFFFFFFF`; false produces zero.
4. Compute the complete `Gpr128` result before generated code assigns it, so
   `rd == rs` and `rd == rt` are safe. Keep the existing guard that suppresses
   writes to GPR[0].
5. Add decoder, IR, codegen, and synthetic generated-native tests, including
   signed boundary values, mixed results, aliasing, zero destination, and
   source/bystander preservation.

## Consequences

- One MMI0 comparison operation is newly executable; the M2 instruction-family
  coverage gate remains open.
- No real-title usage, boot behavior, or compatibility state is claimed.

## Validation

- Focused Rust decoder, IR, and codegen tests cover the exact fields, register
  effects, lowering, and destination suppression.
- Rust format, workspace Clippy (`-D warnings`) and `cargo test --workspace
  --locked` pass on the combined branch that also carries MMI1 PCEQW (ADR 0161):
  362 frontend library tests plus every workspace integration suite.
- Windows x64/MSVC Release validation passes: CMake configure, full build, and
  CTest (45/45), including `reps2_mmi0_pcgtw_codegen_smoke`.
- The fixture uses synthetic instructions at guest base `0x00006F20`.
