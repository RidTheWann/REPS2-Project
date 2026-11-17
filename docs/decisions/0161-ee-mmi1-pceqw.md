# ADR 0161: Decode, lift and AOT-lower R5900 MMI1 PCEQW

- **Status:** Proposed (validated locally; no real-title probe).
- **Date:** 2026-11-17.
- **Scope:** R5900 MMI decoder (`rust/frontend/src/mips.rs`), typed IR
  (`rust/frontend/src/ir.rs`), C++ AOT lowering (`rust/frontend/src/codegen.rs`),
  `Gpr128` helper (`runtime/include/reps2/runtime/ee_state.hpp`), and a
  synthetic generated-code fixture. No runtime ABI or engine state changes.

## Context

PCEQW is an MMI1 operation sharing the `0x28` function value used by other MMI
groups. The encoding is primary opcode `0x1C`, function `0x28`, and selector
bits 10:6 = `0x12`. PS2TeK lists the operation in its MMI1 table; PCSX2's MMI
implementation compares four corresponding 32-bit lanes. Both are external
cross-checks, not a normative hardware specification. The exact references and
limits are recorded in `docs/RESEARCH.md`.

## Decision

1. Decode PCEQW only when the primary opcode, function, and MMI1 selector all
   match. The existing generic R-format decoding supplies `rs`, `rt`, and `rd`;
   other groups and neighboring selectors remain `Unknown`.
2. Add a typed three-GPR IR operation. Its effects read `rs` and `rt`, and
   write `rd` unless `rd` is the architectural zero register.
3. Implement a `constexpr` runtime helper that compares four independent
   32-bit lanes. Each equal lane yields `0xFFFFFFFF`; each unequal lane yields
   zero. Construct the complete `Gpr128` result before codegen assigns it, so
   either source may alias the destination.
4. Emit the helper call behind the existing nonzero-destination guard. Keep
   the operation within the existing generated-source/runtime boundary; no
   per-instruction FFI, new state, or ABI field is introduced.
5. Add a synthetic `compile-words` fixture and native smoke test for lane
   results, `rd == rs`, `rd == rt`, `rd == rs == rt`, GPR[0], and preservation
   of source/bystander registers.

## Consequences

- The frontend and engine support this one additional MMI1 instruction; the
  broader MMI0–MMI3 coverage gate remains open.
- No real-title census or runtime probe is part of this decision. It does not
  establish title usage, boot behavior, or a compatibility-state change.
- The result and evidence limits are documented in `docs/RESEARCH.md`.

## Validation

- Decoder, IR and codegen unit tests cover the MMI1 encoding, neighboring
  selectors, register effects, lowering and GPR[0] suppression.
- `cargo fmt --all -- --check` and `cargo clippy --workspace --all-targets --
  -D warnings` pass; `cargo test --workspace --locked` passes (362 frontend
  library tests plus all workspace integration suites on the combined branch
  that also carries MMI0 PCGTW, ADR 0162).
- Windows x64/MSVC Release validation passes: CMake configure, full build, and
  CTest (45/45), including `reps2_mmi1_pceqw_codegen_smoke`.
- The generated fixture uses synthetic instructions at guest base `0x00006000`;
  it does not use a game artifact or establish title behavior.
