# ADR 0183: Bounded EE Timer 1 registers T1_COUNT/MODE/COMP/HOLD (runtime ABI 32)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`kEeTimer1CountAddress`/`kEeTimer1ModeAddress`/`kEeTimer1CompareAddress`/
  `kEeTimer1HoldAddress`, `EeMemory::timer1_*`, `mmio_read/write_timer1*`,
  MMIO dispatch entries),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 31 -> 32),
  `tests/gs_csr_smoke.cpp` (timer1 storage case), `docs/RESEARCH.md`.
- **Related:** ADR 0131/0134/0142 (Timer 3), ADR 0166/0172 (Timer 2 + INTC),
  ADR 0182 (GIF registers), ADR 0181 (VIF1 FIFO).

## Context

ABI-31 probe `abi31-01` (main `d807103`, ISO `0fe2b777…a7eb86`, boot ELF
`adfda7b7…f0cf0c`) passed the GIF_CTRL/GIF_MODE/GIF_STAT accesses and stopped
at `pc=0x00238B04` after 4,645,682 instructions as
`unsupported_memory_access`. Disassembly/register evidence:
`sw $a0, 0($v1)` with `$v1 = 0x10000810`, `$a0 = 0x82`.

Primary evidence:

- PS2SDK `common/include/ee_regs.h`:
  `A_EE_T1_COUNT = 0x10000800`, `A_EE_T1_MODE = 0x10000810`,
  `A_EE_T1_COMP = 0x10000820`, `A_EE_T1_HOLD = 0x10000830`
  (<https://github.com/ps2dev/ps2sdk/blob/master/common/include/ee_regs.h>).
- PS2 Developer Wiki memory map agrees on the same Timer 1 addresses
  (<https://www.psdevwiki.com/ps2/Memory_Map>).

## Decision

1. Model T1_COUNT, T1_MODE, T1_COMP and T1_HOLD as plain bounded 32-bit
   storage registers in `EeMemory`, reset 0, with MMIO read/write handlers and
   dispatch entries in `kMmioDevices`.
2. **No counting, compare match, overflow, interrupt, clock source, or HOLD
   read-modify behavior is modelled.** A guest that configures and reads back a
   register sees its own written value; nothing else is fabricated.
3. `sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 31 -> 32 and
   ABI-31 modules are refused.

## Consequences

- The ABI-32 probe should pass the T1_MODE write at `pc=0x00238B04` and continue
  into the next init sequence.
- If the title starts Timer 1 and later polls T1_COUNT expecting progression,
  that behavior remains **Unknown** and must fail visibly, not fabricate time.
- No boot stage or Playable claim is made.

## Validation

- `tests/gs_csr_smoke.cpp`: timer1_registers_storage covers count/mode/compare/hold
  write-read round trips.
- `ctest -C Release` 51/51 pass; `cargo fmt/clippy/test` pass.
- Fresh ABI-32 probe recorded in `docs/RESEARCH.md`.
