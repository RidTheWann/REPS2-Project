# ADR 0025: COP0 Register Transfers (MFC0/MTC0) and Interrupt Control (EI/DI/ERET)

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** R5900 COP0 decoder, instruction effects, typed IR, C++20 AOT code generation, runtime EeState modeling, CMake integration, and native smoke test fixture

## Context

System Control Coprocessor 0 (COP0) on the PlayStation 2 Emotion Engine (R5900) manages CPU status, interrupt masks, exception program counters, timers, and processor identification. Across the local PS2 retail library (`D:\List Games`), COP0 instructions appear prominently during early runtime startup, critical section management, and timer polling.

Prior to this increment:
- The REPS2 decoder rejected all primary opcode `0x10` (COP0) instructions as `Opcode::Unknown`.
- Retail games such as `Black (USA)` contained hundreds of COP0 operations (including 340 `mfc0`, 49 `mtc0`, 36 `ei`, 16 `di`, and 3 `eret` instructions).
- No architectural model of COP0 registers existed in `EeState`.

## Decision

**Implement COP0 register movement and status control across the toolchain and runtime:**

### 1. Decoder (`rust/frontend/src/mips.rs`)
- `MFC0 rt, rd`: Primary `0x10`, `rs == 0x00`, bits 10..0 reserved zero. Moves 32-bit COP0 register `rd` into GPR `rt`.
- `MTC0 rt, rd`: Primary `0x10`, `rs == 0x04`, bits 10..0 reserved zero. Moves low 32 bits of GPR `rt` into COP0 register `rd`.
- `EI`: Word `0x42000038` (`rs == 0x10`, funct `0x38`). Sets `Status.EIE` (bit 16).
- `DI`: Word `0x42000039` (`rs == 0x10`, funct `0x39`). Clears `Status.EIE` (bit 16).
- `ERET`: Word `0x42000018` (`rs == 0x10`, funct `0x18`). Returns from exception handler; treated as a basic block terminator (`ControlFlowEffect::Trap`).
- Malformed encodings with non-zero reserved fields decode as `Opcode::Unknown`.

### 2. Instruction Effects
- `MFC0`: Marks GPR `rt` as written (GPR[0] write suppressed).
- `MTC0`: Marks GPR `rt` as read.
- `EI` / `DI`: No GPR dependencies.
- `ERET`: Marks control flow as `ControlFlowEffect::Trap` to terminate basic block discovery in CFG analysis.

### 3. IR & C++20 Codegen (`rust/frontend/src/ir.rs`, `codegen.rs`, `control_codegen.rs`)
- Added typed IR operations:
  - `Operation::MoveFromCop0 { dst, cop0_reg }`
  - `Operation::MoveToCop0 { src, cop0_reg }`
  - `Operation::EnableInterrupts`
  - `Operation::DisableInterrupts`
- Code generation emits:
  - `MFC0`: `if (dst != 0) state.gpr[dst].low = sign_extend_word(read_cop0(state, cop0_reg));`, leaving `state.gpr[dst].high` preserved.
  - `MTC0`: `write_cop0(state, cop0_reg, static_cast<std::uint32_t>(state.gpr[src].low));`.
  - `EI`: `enable_interrupts(state);`.
  - `DI`: `disable_interrupts(state);`.

### 4. Runtime Modeling (`runtime/include/reps2/runtime/ee_state.hpp`)
- Added `std::array<std::uint32_t, 32> cop0{}` to `EeState`.
- Defined constants for essential COP0 registers: `kCop0Index` (0), `kCop0Random` (1), `kCop0EntryLo0` (2), `kCop0EntryLo1` (3), `kCop0Context` (4), `kCop0PageMask` (5), `kCop0Wired` (6), `kCop0BadVAddr` (8), `kCop0Count` (9), `kCop0EntryHi` (10), `kCop0Compare` (11), `kCop0Status` (12), `kCop0Cause` (13), `kCop0Epc` (14), `kCop0PrId` (15), `kCop0Config` (16), `kCop0BadPAddr` (24), `kCop0TagLo` (28), `kCop0TagHi` (29), `kCop0ErrorEpc` (30).
- Modeled `PRId` as read-only constant `0x00002E20` (R5900 implementation 0x2E).
- Initialized `Status` to default reset value `0x10400004`.
- Modeled `EI` and `DI` as atomic toggles of `Status.EIE` (bit 16, mask `0x00010000`).

## Verification

1. **Rust Test Suite:**
   - Unit test `decodes_cop0_transfers_and_control` passes.
   - Unit test `emits_cop0_transfers_and_interrupt_controls` passes.
   - All 135 unit tests pass (100%).
2. **Native C++ Execution:**
   - Implemented `tests/cop0_smoke.cpp` and CMake test target `reps2_cop0_codegen_smoke`.
   - Verified that `PRId` is read-only (attempted `mtc0` write is discarded), `Count` and `Status` round-trip accurately, `EI` and `DI` toggle bit 16, and GPR upper 64-bit lanes remain preserved.
   - All 13 CTest targets pass 100%.
3. **Retail Executables:**
   - 100% of retail test ELFs (`Black`, `Bully`, `Downhill Domination`, `God of War II`, `GTA San Andreas v3.00`, `GTA San Andreas Indonesian v6`, `Need for Speed Most Wanted`, and `Resident Evil 4`) recompile end-to-end cleanly.
