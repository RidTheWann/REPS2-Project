# ADR 0002: typed scalar IR and first C++ AOT vertical slice

- Status: Accepted
- Date: 2026-10-31
- Scope: first executable Rust-IR-to-C++ slice

## Context

A full R5900 translator requires control-flow, delay-slot, memory, exception, and subsystem semantics. Implementing a broad but unverified decoder-to-C++ path would create false compatibility claims. The first useful increment is a small, independently testable straight-line subset whose operations have explicit semantics.

## Decision

- Represent supported operations as typed Rust enum variants and register operands, retaining each guest PC.
- Verify 4-byte PC alignment, strictly sequential instruction layout, and bounded word shift amounts before code generation.
- Emit a deterministic C++20 translation unit for the currently supported arithmetic/logical subset.
- Keep the cross-language boundary as generated C++ plus a later build manifest; no Rust FFI is required.
- Model each EE GPR as two 64-bit lanes (128-bit register shape). The current scalar instructions update the low 64-bit lane only and preserve the high lane.
- Hard-wire GPR[0] to zero at generated function entry and never write a destination of register zero.
- Report ADD/ADDI signed overflow as a structured integer_overflow result. The EE exception vector/CP0 dispatch is not implemented in this slice.
- Guard instructions with the R5900 word-value restriction. An invalid word operand returns undefined_word_operand, which is an REPS2 diagnostic for a specification-undefined case, not a fabricated guest exception.
- Implement ORI's 16-bit immediate as zero-extended, following the MIPS instruction-set definition and observed PCSX2 R5900 behavior. A public mirror of the Sony EE manual contains prose describing ORI's immediate as sign-extended; that statement conflicts with the standard MIPS definition and the R5900 reference implementation, so the discrepancy remains recorded and must be covered by future differential tests.

## Scope

The current linear block supports NOP, ADD, ADDI, ADDU, ADDIU, SUB, SUBU, AND/OR/XOR, ANDI/ORI/XORI, LUI, SLL/SRL/SRA. It does not represent branch/control-flow edges, delay slots, loads/stores, LL/SC, CP0/FPU/MMI, exceptions as complete machine-state transitions, or runtime services.

## Validation

CMake asks the Rust frontend to compile a synthetic instruction stream into C++, compiles the emitted translation unit with the platform C++20 compiler, and executes it against a minimal EE GPR state test harness. Tests verify arithmetic results, zero-register behavior, sign extension, preservation of the upper GPR lane, overflow reporting, and explicit rejection of an undefined word operand.
