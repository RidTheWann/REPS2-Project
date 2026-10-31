# ADR 0014: base EE HI/LO and R5900 three-operand MULT/MULTU

- **Status:** Accepted
- **Date:** 2026-10-31
- **Scope:** Base EE HI/LO state, moves, and the documented R5900 three-operand signed/unsigned multiply subset

## Context

The decoder exposed the mnemonics MFHI, MFLO, MULT and MULTU but did not lift them into the scalar IR. In addition, its MULT/MULTU decode predicates incorrectly required `rd = 0`, rejecting the R5900 three-operand forms. The EE's base HI/LO state was not represented in the C++ runtime ABI.

The R5900 is not interchangeable with a generic MIPS32 implementation for these operations. The TX79 architectural description identifies a three-operand multiply form in which the low product word is written to both the selected GPR and LO, and the high product word is written to HI. PCSX2 independently implements this behavior and the 64-bit EE lane handling.

## Decision

Add base `hi` and `lo` 64-bit fields to `EeState`. The current runtime represents only this base pair; HI1/LO1 (MMI pipeline 1) is explicitly not modeled. The ELF-entry state initializer clears both special registers.

Decode and lift:
- `MTHI rs`: copy GPR[rs][63:0] to HI.
- `MTLO rs`: copy GPR[rs][63:0] to LO.
- `MFHI rd`: copy HI to GPR[rd][63:0].
- `MFLO rd`: copy LO to GPR[rd][63:0].
- `MULT rd, rs, rt`: multiply the low 32-bit signed operands.
- `MULTU rd, rs, rt`: multiply the low 32-bit unsigned operands.

For MULT/MULTU, the low 32 product bits are sign-extended into LO and copied to a non-zero GPR `rd`; the high 32 product bits are sign-extended into HI. A zero `rd` still updates HI/LO, while GPR[0] remains hard-wired to zero. The generated code snapshots both operands before writing any destination, so `rd` aliasing either source is safe.

## Architectural boundaries

This does not add:
- multiply pipeline timing or interlock behavior;
- HI1/LO1, MULT1/MULTU1, MADD/MADDU, MMI accumulators, or MMI0–MMI3;
- DIV/DIVU, whose divide-by-zero and 64-bit result-lane behavior must be separately researched and tested;
- complete exception delivery, interrupts, or a full EE execution environment.

Decode/lift support and generated execution support are distinct gates. Only the six operations above are added to the executable IR; other special-register or multiply-family opcodes must remain explicit unsupported instructions.

## Validation

Synthetic tests cover:
- valid three-operand MULT/MULTU encodings with non-zero `rd`, and MULT to GPR zero;
- signed multiply boundary cases including INT32_MIN × INT32_MAX, INT32_MIN × -INT32_MAX, and INT32_MIN × INT32_MIN;
- source/destination aliasing for both signed MULT and unsigned MULTU;
- reserved-field rejection for MTHI/MTLO and malformed multiply encodings;
- GPR and HI/LO read/write effect metadata;
- typed IR lifting for all six operations;
- generated C++ execution for MTHI/MTLO/MFHI/MFLO, signed MULT and unsigned MULTU;
- preservation of GPR[127:64], alias-safe multiply lowering, sign extension of both product halves, and final HI/LO state.

CI is the authoritative host-compiler check on Windows/MSVC and Linux/GCC. Passing the synthetic fixture is not evidence of retail-title compatibility.

## References

- QEMU's R5900 three-operand MULT/MULTU implementation and explicit state-transfer definition: https://patchew.org/QEMU/cover.1540134918.git.noring@nocrew.org/ff1edd3989b06e9cae67c962cec79081e4d7af0a.1540134918.git.noring@nocrew.org/
- QEMU's independent signed MULT test vectors, including INT32_MIN edge cases: https://patchew.org/QEMU/cover.1540134918.git.noring@nocrew.org/b72eb39dd9c465d110e3df2e3660c32c979d0e4d.1540134918.git.noring@nocrew.org/
- PCSX2 R5900 interpreter behavior for MULT/MULTU, MFHI/MFLO and MTHI/MTLO: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp
- REPS2 [decoder](../../rust/frontend/src/mips.rs), [typed IR](../../rust/frontend/src/ir.rs), and [runtime ABI](../../runtime/include/reps2/runtime/ee_state.hpp).
