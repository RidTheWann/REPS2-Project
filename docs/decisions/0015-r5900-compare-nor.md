# ADR 0015: R5900 64-bit comparisons and NOR

- **Status:** Accepted
- **Date:** 2026-10-31
- **Scope:** NOR, SLT, SLTU, SLTI, and SLTIU scalar execution in the current 64-bit EE GPR lane model

## Context

The decoder already recognized the five operations, and the instruction-effect metadata already tracked their source and destination GPRs. However, they were not represented in typed IR or native code generation, so CFG lifting stopped when it encountered them.

The EE presents 128-bit GPRs, but these scalar instructions operate on the low 64-bit lane and preserve the upper lane. The signed and unsigned comparisons differ at bit 63. Immediate comparisons have a subtle rule: both SLTI and SLTIU sign-extend the 16-bit immediate; SLTIU then performs an unsigned 64-bit comparison.

## Decision

Add typed IR operations:
- `Nor64`
- `SetLessThanSigned64`
- `SetLessThanUnsigned64`
- `SetLessThanImmediateSigned64`
- `SetLessThanImmediateUnsigned64`

Generated C++ writes exactly 0 or 1 to GPR[rd/rt][63:0], suppresses writes to GPR[0], and preserves GPR[127:64]. Signed comparisons reinterpret the low-lane bit pattern with C++20 `std::bit_cast<std::int64_t>`; unsigned comparisons use `std::uint64_t` directly. The immediate is first converted from the encoded `u16` to signed `i16`, then widened to 64 bits. For SLTIU, the resulting signed constant is converted modulo 2^64 to unsigned.

NOR computes `~(rs.low | rt.low)` in 64 bits.

## Validation

- Decoder tests assert the SPECIAL and immediate opcode mappings, register fields, and signed immediate extraction.
- Effect tests verify GPR source/destination dependencies and no memory/exception side effects.
- IR tests verify typed operation lifting.
- CFG tests verify comparisons and NOR remain in executable body blocks.
- The generated native smoke fixture tests signed boundary values, unsigned order reversal, negative immediate sign extension, GPR[0], NOR result bits, and upper-lane preservation on Windows/MSVC and Linux/GCC.

## Reference

PCSX2 R5900 interpreter implementation, used only as a behavioral cross-check for the independently authored REPS2 implementation: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp (SLTI/SLTIU at approximately lines 2662–2663; NOR/SLT/SLTU at approximately lines 2744–2748).
