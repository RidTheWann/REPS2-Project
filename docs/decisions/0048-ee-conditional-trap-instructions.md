# ADR 0048: EE conditional trap instruction lowering and precise exceptions

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** R5900 decoder, typed scalar IR, C++ AOT lowering, Level-1 Trap delivery and delay-slot semantics

## Context

The EE supports twelve conditional trap instructions that were previously decoded as unknown and therefore blocked translation:

- Register operands: `TGE`, `TGEU`, `TLT`, `TLTU`, `TEQ`, `TNE`.
- Immediate operands: `TGEI`, `TGEIU`, `TLTI`, `TLTIU`, `TEQI`, `TNEI`.

These are conditional synchronous exceptions, not control-flow terminators: a false predicate retires normally and execution continues at the next instruction. A true predicate raises the Level-1 Trap exception. The instructions can appear in branch delay slots, where EPC must identify the owning branch and Cause.BD must be set. A not-taken branch-likely must annul the instruction, including its possible exception.

## Architectural evidence and resolved manual inconsistency

Sony's *EE Core Instruction Set Manual*, version 6.0, pages 123–134, documents the twelve encodings, the optional ten-bit code field in register-register forms, and the low 64-bit GPR operands. The immediate forms use a sign-extended 16-bit immediate. In particular, `TGEIU` and `TLTIU` first sign-extend the immediate and then perform an unsigned 64-bit comparison; they do not zero-extend the 16-bit field.

Sony's *EE Core User's Manual*, version 6.0, section 4.2.15 and the exception reference table assign Trap ExcCode 13, separate from Integer Overflow ExcCode 12. The same section's later Operation bullet contains an inconsistent `Cause.ExcCode : 12` line. That conflicts with its own section heading/table, the master exception list, and the separate Overflow code. REPS2 follows the architectural exception table and models Trap as **ExcCode 13**.

Primary references:
- Sony, *EE Core Instruction Set Manual* v6.0, TEQ through TNEI, pp. 123–134: https://docs.alexrp.com/mips/ee_insns.pdf
- Sony, *EE Core User's Manual* v6.0, section 4.2.15 (Trap Exception): https://docs.alexrp.com/mips/ee.pdf

## Decision

### 1. Preserve the instruction encoding fields

- SPECIAL funct values: `TGE=0x30`, `TGEU=0x31`, `TLT=0x32`, `TLTU=0x33`, `TEQ=0x34`, `TNE=0x36`.
- REGIMM rt selectors: `TGEI=0x08`, `TGEIU=0x09`, `TLTI=0x0A`, `TLTIU=0x0B`, `TEQI=0x0C`, `TNEI=0x0E`.
- Bits 15..6 in the six SPECIAL encodings are an optional ten-bit software code field, not reserved-zero fields. The decoder accepts and retains their raw instruction word rather than rejecting nonzero code values.

### 2. Use a typed conditional predicate

The scalar IR represents a comparison kind, a validated GPR lhs, and either a validated GPR rhs or a signed `i16` immediate. The instruction remains an ordinary body operation; it is not marked as an unconditional terminator or a branch.

Semantics are defined on `GPR[63:0]` only. Signed predicates bit-cast the full low 64-bit value to `int64_t`; unsigned predicates compare the low 64-bit values as `uint64_t`. Equality compares the exact low-lane bit patterns. No operation reads or writes GPR[127:64], and no destination register exists.

For an immediate `imm16`, all comparisons use `sign_extend(imm16)` to 64 bits first. Signed variants use signed ordering; unsigned variants compare the resulting 64-bit bit pattern as unsigned. This is especially important for negative `TGEIU`/`TLTIU` immediates: `-2` becomes `0xFFFFFFFFFFFFFFFE`, not `0x000000000000FFFE`.

### 3. Deliver the shared Level-1 Trap exception

Add `ExceptionCode::Trap = 13` and reuse `raise_ee_exception`. A true predicate sets Cause.ExcCode, sets EXL, chooses the BEV-aware V_COMMON vector, and records EPC/BD using the existing common exception-entry implementation. The emitted call passes the scalar instruction's branch-delay context through unchanged.

A false predicate performs no state write and returns normally through the current basic block. The optional code field does not affect the predicate; it remains encoded in the guest instruction so a guest handler can inspect the instruction word at EPC.

### 4. Preserve control-flow ordering

Conditional traps are not basic-block terminators and have no delay slot of their own. When used as a branch delay-slot instruction, they execute after the branch's link write and before the selected target/fallthrough PC is committed. When a branch-likely is not taken, the delay-slot conditional trap is not executed and cannot raise an exception.

## Validation

- Decoder tests cover all twelve encodings, optional code field acceptance, no control-transfer classification, operand dependencies and possible-exception metadata.
- IR tests cover each typed comparison and exact signed-immediate preservation.
- Codegen tests pin signed/unsigned 64-bit comparisons and sign-extend-then-unsigned immediate emission.
- Native CFG tests cover true and false predicates for all twelve instructions, preservation of both GPR lanes, ExcCode 13, precise EPC/BD, delay-slot delivery and branch-likely annulment.
- The same CFG fixture runs in both single-translation-unit and split-translation-unit CMake tests on Windows/MSVC and Linux/GCC.

**Verified evidence:** GitHub Actions CI run #373 (`41513659169`) completed successfully on all three jobs. Both Ubuntu/GCC and Windows/MSVC passed Rustfmt, Clippy, Rust unit tests, CMake configure/build, and all 16 CTest cases; the platform-specific CLI smoke tests passed. The Rust 1.88 MSRV unit-test job also passed. In particular, both `reps2_cfg_codegen_smoke` and `reps2_cfg_split_codegen_smoke` passed with the twelve conditional-trap true/false vectors and delay-slot/branch-likely coverage.

## Consequences

- The twelve conditional trap instructions become supported AOT operations rather than unsupported stops.
- Exception entry remains centralized, and branch-likely annulment is preserved.
- The implementation does not claim coverage for asynchronous interrupts, debug traps, TLB/bus/permission faults, or other unmodeled exception sources.
