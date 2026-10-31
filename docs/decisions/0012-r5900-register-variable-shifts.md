# ADR 0012: Register-variable R5900 shifts

- Status: Accepted
- Date: 2026-10-31

## Context

The R5900 integer pipeline needs both immediate and register-variable shifts. Variable shifts are used for bitfield operations, pointer/index calculations, and compiler-generated integer code. Implementing them as a shared decoder/IR/backend feature preserves the title-agnostic recompiler architecture.

## Decision

Implement the following SPECIAL-function operations through decode, typed IR and C++20 AOT code generation:

- SLLV, SRLV, SRAV: take the shift count from the low five bits of the source-count GPR.
- DSLLV, DSRLV, DSRAV: take the shift count from the low six bits of the source-count GPR.

The data operand is GPR[63:0]. Word operations use the low 32 bits and sign-extend the 32-bit result to GPR[63:0]. Doubleword operations use all 64 bits. All operations preserve GPR[127:64], ignore writes to GPR[0], and have no arithmetic-overflow trap.

Reserved shift-amount encoding bits must be zero; other encodings remain unsupported. The generated code explicitly masks the count to avoid undefined C++ shifts by the operand width. Arithmetic-right helpers use defined sign-fill logic.

## Validation

Decoder and IR tests cover all six encodings and their register mappings. Codegen tests check the mask widths and operation lowering. A CMake-generated instruction stream is compiled and executed as native C++ on Windows/MSVC and Linux/GCC. Its count register is initialized to 0xC5 so both masks are exercised (word count 5, doubleword count 5), while expected results verify word sign extension, arithmetic sign fill, and upper-lane preservation.

These tests demonstrate isolated instruction behavior only; they do not establish a real-game boot or compatibility result.
