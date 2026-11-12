# ADR 0132: R5900 MADD/MADDU HI/LO accumulation

- **Status:** Accepted.
- **Date:** 2026-11-13.
- **Scope:** SPECIAL2 function `0x00` (MADD) and `0x01` (MADDU) decode, IR lift, and AOT lowering.

## Context

The seven-title reachable census counted MMI1 `MADDU` (18 words) and `MADD` (9 words) in the Downhill Domination translation units, so both are reachable on the current real-title path. Both are encoded under primary opcode `0x1C` (SPECIAL2) with function bits `0x00`/`0x01`; the `rd` and `sa` fields are ignored.

- **Observed:** sample words from the census decode as `unknown` before this change (`reps2 decode`).
- **Derived:** R5900 semantics are `{HI,LO} += sext32(rs) * sext32(rt)` for MADD and `{HI,LO} += zext32(rs) * zext32(rt)` for MADDU, with the 64-bit sum wrapping and HI/LO stored as sign-extended words, matching the existing MULT/MULTU storage convention in `emit_multiply_word`.

## Decision

1. Decode function `0x00` as `MultiplyAccumulateSigned` (`madd`) and `0x01` as `MultiplyAccumulateUnsigned` (`maddu`). Effects: read `rs`/`rt`, read and write base `HI`/`LO`, write no GPR, no exception, no delay slot.
2. Lift to `Operation::MultiplyAccumulateSigned/Unsigned { lhs, rhs }`.
3. Lower to generated C++ that forms the 64-bit product, reads the HI:LO pair from the low 32 bits of `state.hi`/`state.lo`, adds with wrapping, and stores both words sign-extended — the same convention MULT/MULTU use.

## Consequences

- MADD/MADDU leave the unsupported set; the remaining SPECIAL2 space (MUL, MADD1/MADDU1, MMI1 selectors such as PMADDH/PMADDW/PMSUBH, etc.) stays explicitly unsupported.
- No `EeState` layout change, so `REPS2_RUNTIME_ABI_VERSION` stays 2.
- HI1/LO1 and the MMI pipeline-1 accumulators remain unmodeled.

## Validation

- Decoder tests pin opcode, mnemonic, effects (GPR read mask excludes hard-wired GPR[0], HI/LO read+write, no GPR write), and that other SPECIAL2 function slots stay `Unknown`.
- IR test lifts both words and verifies the program.
- Codegen test asserts the sign-extended/zero-extended operand forms and the wrapping pair accumulation for both instructions.
- End-to-end: the synthetic HI/LO codegen fixture gains `0x70220000` (MADD $1,$2) and `0x70620001` (MADDU $3,$2) and asserts the final `HI=0x2F`, `LO=0xFFFFFFFFFFFF6400` after the wrapping sums (verified against an independent BigInt simulation of the same semantics).
- Local verification: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace` (374 passed, 0 failed), CMake Release build and `ctest -C Release` (40/40) pass.
