# ADR 0011: Non-trapping R5900 doubleword scalar lowering

- Status: Accepted
- Date: 2026-10-31

## Context

The Emotion Engine stores 128-bit GPRs, while common scalar operations work on the low 64-bit doubleword lane. The current compiler subset previously lacked these basic 64-bit arithmetic and shift operations.

## Decision

Implement this bounded set end-to-end through decoder, typed IR, C++20 emitter, and the existing execution ABI:

- DADDU and DSUBU use modulo-2^64 arithmetic on GPR[63:0].
- DADDIU sign-extends its 16-bit immediate and ignores overflow.
- DSLL, DSRL, and DSRA use shift amounts 0 through 31.
- DSLL32, DSRL32, and DSRA32 use shift amounts 32 through 63.

These operations update only the low 64-bit lane and preserve GPR[127:64]. Writes to GPR[0] are discarded. Arithmetic right shift uses explicit unsigned shifts and sign-fill masks; generated code does not rely on host signed right-shift behavior.

DADD, DSUB, and DADDI remain unsupported until precise signed-overflow exception delivery exists. They must not be treated as non-trapping aliases.

## Validation

Decoder tests cover encodings, branch-slot classification, and reserved shift fields. IR tests cover register mapping, sign-extended immediate retention, 32-shift amount expansion, and rejection of amounts outside 0-63. Codegen tests verify output structure. CMake compiles and executes a synthetic instruction stream on Windows/MSVC and Linux/GCC covering wraparound, negative immediates, 32-bit shift extensions, and upper GPR-lane preservation.

Synthetic tests do not establish compatibility with a retail game.
