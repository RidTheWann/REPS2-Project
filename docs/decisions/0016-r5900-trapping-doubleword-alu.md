# ADR 0016: trapping R5900 64-bit DADD, DSUB and DADDI

- **Status:** Accepted
- **Date:** 2026-10-31
- **Scope:** Signed 64-bit arithmetic and precise destination behavior for DADD, DSUB and DADDI

## Context

The previous experimental branch added decoding and scalar IR variants for DADD, DSUB and DADDI, but stopped before backend lowering. Its branch CI consequently failed Rust's exhaustive match check in `codegen.rs`. These operations are not interchangeable with DADDU, DSUBU and DADDIU: the signed forms raise an integer-overflow exception, while the unsigned-named variants wrap modulo 2^64.

The R5900 interpreter reference also makes an important ordering rule explicit: overflow is checked before skipping a write to GPR[0]. An overflowing DADD/DSUB/DADDI still faults when the encoded destination is register zero.

## Decision

Add distinct typed scalar operations `AddDoublewordSigned`, `SubDoublewordSigned`, and `AddImmediateDoublewordSigned`. Add exact SPECIAL-function decoding for DADD (0x2C) and DSUB (0x2E), plus primary opcode DADDI (0x18). Effect metadata marks all three as reading their sources, writing the encoded destination, and potentially raising a synchronous exception.

The C++ backend performs the arithmetic using unsigned 64-bit result bits:
- DADD overflow: `(~(lhs ^ rhs) & (lhs ^ result) & sign_bit) != 0`.
- DSUB overflow: `((lhs ^ rhs) & (lhs ^ result) & sign_bit) != 0`.
- DADDI uses the addition identity after sign-extending its 16-bit immediate to 64 bits.

Unsigned result arithmetic is defined modulo 2^64, so the host C++ implementation never evaluates an overflowing signed expression. The backend checks overflow first, returns `Status::integer_overflow` on a fault, and only writes a non-zero destination after the check succeeds. The GPR[0] normalization remains intact.

## Validation

- Decoder and effect tests cover opcode fields, signed immediate extraction, reserved shift bits, source masks, destinations, and exception metadata.
- Typed IR tests distinguish all three trapping operations from DADDU/DSUBU/DADDIU.
- CFG tests verify the operations can be lifted as body instructions.
- Codegen tests check the overflow predicates and signed-immediate conversion.
- A generated native block is executed with normal and faulting states. Tests cover `INT64_MAX + 1`, `INT64_MIN - 1`, `INT64_MIN + (-2)`, source/destination aliasing, prior-instruction commit behavior, and overflow with `rd = 0`.
- Windows/MSVC and Linux/GCC CI are required for merge.

## Reference

PCSX2's R5900 interpreter, used as a behavioral cross-check for R5900 overflow and GPR[0] ordering rules; REPS2's decoder, IR and native lowering are independently authored: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp.
