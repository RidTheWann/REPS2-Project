# ADR 0186: Bounded VIF1_STAT (0x10003C00) storage register (runtime ABI 35)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`kEeVif1StatAddress`, `EeMemory::vif1_stat`,
  `mmio_read/write_vif1_stat`, MMIO dispatch entry, 64-bit decomposition),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 34 -> 35),
  `tests/gs_csr_smoke.cpp`, `docs/RESEARCH.md`.
- **Related:** ADR 0179 (VIF1 FBRST/ERR), ADR 0181 (VIF1 FIFO),
  ADR 0185 (GIF DMAC channel 2), ADR 0182 (GIF registers).

## Context

ABI-34 probes `abi34-01`..`abi34-03` (main `0d7cbe9`, ISO `0fe2b777…a7eb86`,
boot ELF `adfda7b7…f0cf0c`) consistently stopped at `pc=0x002433E0` after
4,670,452 instructions as `unsupported_memory_access`. Disassembly/register
evidence: `lw $a2, 0($s0)` with `$s0 = 0x10003C00`.

Primary evidence:

- PS2SDK `common/include/ee_regs.h`: `A_EE_VIF1_STAT = 0x10003C00`,
  `R_EE_VIF1_STAT = (vu32*)A_EE_VIF1_STAT`, `RD_EE_VIF1_STAT`
  (<https://github.com/ps2dev/ps2sdk/blob/master/common/include/ee_regs.h>).
- PS2Tek VIF I/O register reference documents VIF1_STAT fields: VPS/VEW/VGW/
  MRK/DBF/VSS/VFS/VIS/INT/ER0/ER1/FDR/FQC
  (<https://israpps.github.io/ps2tek/PS2/VIF/IO_Regs.html>).
- Sony EE User's Manual register map lists `0x1000_3C00` as VIF1_STAT
  (<https://docs.alexrp.com/mips/ee.pdf>).

## Decision

1. Model VIF1_STAT (`0x10003C00`) as a plain bounded 32-bit storage register,
   reset 0, with MMIO read/write handlers and a dispatch entry.
2. 64-bit SD/LD decomposition includes the new address.
3. **No VIF command parsing, FIFO occupancy update, transfer status, stall,
   interrupt, or FQC status side effect is modelled.** A guest polling
   VIF1_STAT sees only the stored/reset value; no hardware readiness is
   fabricated.
4. `sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 34 -> 35 and
   ABI-34 modules are refused.

## Consequences

- The ABI-35 probe should pass the VIF1_STAT read at `pc=0x002433E0` and
  continue into the next VIF/GIF init sequence.
- If the title polls VIF1_STAT for hardware-updated VPS/FQC/stall bits, that
  behavior remains **Unknown** until separately evidenced; this ADR does not
  fabricate it.
- No boot stage or Playable claim is made.

## Validation

- `tests/gs_csr_smoke.cpp`: `vif1_stat_storage` covers reset-zero read and
  write-read round trip.
- `ctest -C Release` 51/51 pass; `cargo fmt/clippy/test` pass.
- Fresh ABI-35 probe recorded in `docs/RESEARCH.md`.
