# ADR 0049: EE MMI0 PADDW/PSUBW modulo lane arithmetic

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** R5900 MMI0 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR lane semantics

## Context

The initial MMI implementation supported the saturating unsigned MMI1 instruction PADDUW, but did not recognize the basic MMI0 PADDW and PSUBW operations. Both are used for 128-bit SIMD integer arithmetic and can be translated without introducing new architectural state or exceptions.

## Architectural evidence

Sony's *EE Core Instruction Set Manual*, version 6.0, documents PADDW on page 174 and PSUBW in the corresponding MMI0 instruction entry. The instruction encoding is:

- Primary opcode: `MMI = 0x1C`.
- Low six-bit group selector: `MMI0 = 0x08`.
- Five-bit function field in bits 10..6: `PADDW = 0x00`, `PSUBW = 0x01`.
- Standard `rs`, `rt`, and `rd` fields in bits 25..21, 20..16, and 15..11.

The operation descriptions state that the 128-bit source GPRs are split into four 32-bit words and corresponding words are added or subtracted. The result retains only bits 31..0 of each lane result. There is no carry or borrow across lane boundaries and no exception is defined.

Primary reference:
- Sony, *EE Core Instruction Set Manual* v6.0, PADDW, p. 174: https://docs.alexrp.com/mips/ee_insns.pdf
- Sony EE MMI0 opcode table cross-check: https://israpps.github.io/ps2tek/

## Decision

### 1. Keep MMI0 distinct from MMI1

Decode only the exact MMI0 selector `0x08` with function field `0` or `1`. Do not alias the MMI1 selector `0x28`, PADDUW function fields, or reserved MMI0 function values to these operations. Preserve the standard register fields and classify the encoding family as MMI0.

### 2. Use explicit typed IR operations

Add distinct `ParallelAddWord` and `ParallelSubWord` operations. Each records a validated destination, left source, and right source GPR. Decoder effects read both sources, write the destination, and report no synchronous exception or control transfer.

### 3. Define lane behavior without signed overflow

Treat each 32-bit word as an unsigned bit-vector for the host implementation. For lane (i in {0,1,2,3}):

`PADDW: dst[i] = (lhs[i] + rhs[i]) mod 2^32`

`PSUBW: dst[i] = (lhs[i] - rhs[i]) mod 2^32`

All four lanes are calculated independently and packed into the complete 128-bit destination GPR. This makes wraparound defined in C++ and avoids signed-overflow undefined behavior. Unlike PADDUW, neither operation saturates.

### 4. Preserve GPR destination rules

Generated code assigns the complete 128-bit helper result, preserving correct behavior when `rd` aliases either source because the helper returns a value before assignment. Writes to architectural GPR[0] remain suppressed by the generated destination guard.

## Validation

- Decoder tests assert exact MMI0 selector/function matching, mnemonic, register fields, family classification, dependency masks, and rejection of reserved or neighboring MMI-group encodings.
- IR tests assert separate typed PADDW and PSUBW operations.
- Codegen tests assert the correct modulo-lane runtime helper calls and zero-register guard.
- Runtime/native tests cover all four lanes, addition overflow, subtraction underflow, absence of cross-lane carry/borrow, full 128-bit writes, destination aliasing with either source, and GPR[0] suppression.
- The generated C++ fixture is compiled and executed by CMake/CTest on Windows/MSVC and Linux/GCC; Rust MSRV 1.88 remains part of the required CI matrix.

**Verified evidence:** GitHub Actions CI run #381 (`41516895333`) completed successfully. Ubuntu/GCC and Windows/MSVC each passed Rustfmt, Clippy, 215 Rust unit tests, CMake configure/build, all 17 CTest cases, and the platform-specific CLI smoke test. The Rust 1.88 MSRV unit-test job also passed. `reps2_mmi0_word_codegen_smoke` passed on both native toolchains.
