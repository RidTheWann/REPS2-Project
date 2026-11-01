# ADR 0023: Memory Barrier (SYNC), Integer Division (DIV/DIVU), and Conditional Moves (MOVN/MOVZ)

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** R5900 decoder, instruction effects, typed IR, C++20 AOT code generation, CMake integration, retail ELF coverage census, and native smoke testing

## Context

Following the completion of the COP1 compare and BC1 branch slice (ADR 0022), the linear entry scan over all eight sampled retail ELF executables from `D:\List Games` was blocked at word 67 (`0x0000040F`, PC `0x00100114`), which is `SYNC` (SPECIAL funct `0x0F`).

Furthermore, analysis of the remaining unsupported instruction families showed that the `special` family comprised 58.3% (381,245 words) of all unsupported instructions. Within the SPECIAL group:
- `DIV` and `DIVU` had been decoded in `mips.rs` but were not lifted into executable scalar IR or lowered in the C++20 backend.
- Conditional moves `MOVN` (funct `0x0B`) and `MOVZ` (funct `0x0A`) were present in substantial quantities across retail code.
- Cache and prefetch instructions (`CACHE` primary `0x2F`, `PREF` primary `0x33`) appeared throughout retail games but had no guest-visible register state consequences for static recompilation.

## Decision

**Implement SYNC, MOVN, MOVZ, CACHE, and PREF in the decoder, and provide complete typed IR lifting and C++20 lowering for DIV, DIVU, MOVN, MOVZ, and fence/hint no-ops.**

### Decoder (`rust/frontend/src/mips.rs`)

1. **SYNC (SPECIAL funct `0x0F`):**
   - Decoded when `rs == 0`, `rt == 0`, and `rd == 0`.
   - The `stype` field (bits 10..6) selects the barrier type. The Sony EE manual documents `stype = 0` (completion barrier), while software encodings like `0x0000040F` (`stype = 16`) occur in retail boot sequences. All valid `stype` fields decode as `Opcode::Sync`.
   - Has no delay slot and sets no register read/write masks.

2. **MOVZ / MOVN (SPECIAL functs `0x0A` and `0x0B`):**
   - Standard R-type encoding (`rs`, `rt`, `rd`) with `sa` (bits 10..6) reserved zero. Non-zero `sa` values decode to `Opcode::Unknown`.
   - `MOVZ`: `GPR[rd] = GPR[rs]` if `GPR[rt] == 0`.
   - `MOVN`: `GPR[rd] = GPR[rs]` if `GPR[rt] != 0`.
   - Effects mark `rs` and `rt` as read, and `rd` as written (conservative analysis).

3. **CACHE (primary `0x2F`) and PREF (primary `0x33`):**
   - `CACHE`: operation in `rt`, base in `rs`, signed 16-bit offset.
   - `PREF`: hint in `rt`, base in `rs`, signed 16-bit offset.
   - For static recompilation to Windows x64 host code, these are hints with no guest-visible register or memory state modifications.

### IR and C++20 Lowering (`rust/frontend/src/ir.rs`, `codegen.rs`)

1. **Integer Division (`DIV` and `DIVU`):**
   - R5900 division places the 32-bit quotient in `LO` and 32-bit remainder in `HI`, sign-extending both 32-bit results into the 64-bit HI/LO representation.
   - **Zero Divisor Semantics:**
     - `DIV`: For positive dividend $A \ge 0$, quotient is $-1$ (`0xFFFFFFFFFFFFFFFF`), remainder is $A$. For negative dividend $A < 0$, quotient is $1$ (`0x0000000000000001`), remainder is $A$.
     - `DIVU`: Quotient is `UINT32_MAX` sign-extended (`0xFFFFFFFFFFFFFFFF`), remainder is $A$.
   - **Signed Overflow:**
     - `INT32_MIN / -1`: Quotient is `INT32_MIN` (`0xFFFFFFFF80000000`), remainder is `0`.
   - The emitted C++20 guards against host-language undefined behavior (division by zero and signed integer overflow) using explicit condition branches before evaluating division/modulo operators.

2. **Conditional Moves (`MOVN` / `MOVZ`):**
   - Emits a conditional assignment that updates only `state.gpr[rd].low`, preserving `state.gpr[rd].high`.
   - Hard-wired zero register `GPR[0]` is explicitly guarded against modification.

3. **Fences and Hints (`SYNC`, `CACHE`, `PREF`):**
   - Lifted directly to `Operation::Nop`, generating no state side effects while remaining verifiable.

## Evidence

1. **Rust Unit Tests:**
   - 132/132 tests passing in `reps2-frontend`.
   - Validates decoding, reserved-field rejection (e.g., non-zero `sa` in MOVN/MOVZ), instruction effects, and CFG lifting.

2. **Native C++ Smoke Fixture (`tests/div_mov_smoke.cpp`):**
   - Executed as `reps2_div_mov_codegen_smoke` in CTest (12/12 test targets passing 100%).
   - Exercises normal signed DIV, divide-by-zero (positive and negative dividends), `INT32_MIN / -1` signed overflow, normal unsigned DIVU, unsigned divide-by-zero, taken/not-taken MOVZ, taken/not-taken MOVN, destination-zero suppression, and upper 64-bit GPR lane preservation.

3. **Retail Executable Profiling:**
   - Evaluated across all eight sampled retail ELF executables from `D:\List Games`.
   - **Total Scanned Words:** 7,814,471 (8 images) / 6,393,607 (7 distinct cores).
   - **Supported Words:** Increased from 7,146,362 (91.45%) to 7,164,374 (91.68%) on 8 images, and from 5,825,265 (91.11%) to 5,841,092 (91.36%) on 7 distinct cores.
   - **Unsupported Words:** Dropped by 18,012 words (from 654,380 to 636,368).
   - **Linear Entry Scan Breakthrough:**
     - All retail titles (Bully, Downhill Domination, God of War II, GTA San Andreas, Need for Speed Most Wanted, Resident Evil 4) now decode cleanly from their ELF entry points (e.g. 112 or 92 consecutive words) without encountering any unsupported opcode.
     - Every sampled title now successfully reaches its first kernel/BIOS `SYSCALL` instruction (`0x0000000C`) without stalling.
