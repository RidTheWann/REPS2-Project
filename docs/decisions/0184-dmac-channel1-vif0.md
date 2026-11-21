# ADR 0184: Bounded VIF0 DMAC channel-1 registers CHCR/MADR/QWC/TADR (runtime ABI 33)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `runtime/include/reps2/runtime/ee_dmac.hpp`
  (channel-1 fields/accessors, `kEeDmacChcr1Address`/`kEeDmacMadr1Address`/
  `kEeDmacQwc1Address`/`kEeDmacTadr1Address`),
  `runtime/include/reps2/runtime/ee_state.hpp` (MMIO dispatch entries and
  `mmio_read/write_*1` handlers),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 32 -> 33),
  `tests/gs_csr_smoke.cpp`, `docs/RESEARCH.md`.
- **Related:** ADR 0086/0091/0100 (SIF0 channel-5 model), ADR 0183 (Timer 1),
  ADR 0182 (GIF), ADR 0181 (VIF1 FIFO).

## Context

ABI-32 probe `abi32-01` (main `d49b4b7`, ISO `0fe2b777…a7eb86`, boot ELF
`adfda7b7…f0cf0c`) passed the Timer 1 MODE write and stopped at
`pc=0x002433D0` after 4,670,452 instructions as `unsupported_memory_access`.
Disassembly/register evidence: `lw $v1, 0($s0)` with `$s0 = 0x10009000`.

Primary evidence:

- PS2SDK `common/include/ee_regs.h` DMAC register map:
  `A_EE_D1_CHCR = 0x10009000`, `A_EE_D1_MADR = 0x10009010`,
  `A_EE_D1_QWC = 0x10009020`, `A_EE_D1_TADR = 0x10009030`
  (<https://github.com/ps2dev/ps2sdk/blob/master/common/include/ee_regs.h>).
- PS2 Developer Wiki memory map agrees; DMAC channel 1 is VIF0
  (<https://www.psdevwiki.com/ps2/Memory_Map>).

## Decision

1. Model channel-1 CHCR/MADR/QWC/TADR as bounded 32-bit storage registers in
   `EeDmacState`, with read/write accessors and MMIO dispatch entries.
2. Unlike the seeded SIF0 channel-5 model, channel-1 registers are writable
   without prior seeding and read back what was written; reset state is 0/unknown
   until first write, and reads before any write fail closed.
3. **No DMA transfer, VIF0 FIFO interaction, interrupt, or completion event is
   modelled.** Writing CHCR.STR does not start anything in this ADR.
4. `sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 32 -> 33 and
   ABI-32 modules are refused.

## Consequences

- The ABI-33 probe should pass the channel-1 register access at `pc=0x002433D0`
  and continue into the next init sequence.
- If the title starts a VIF0 DMA and polls for completion, that behavior remains
  **Unknown** and must fail visibly, not fabricate transfer completion.
- No boot stage or Playable claim is made.

## Validation

- `tests/gs_csr_smoke.cpp`: `dmac_ch1_registers_storage` covers CHCR/MADR/QWC/TADR
  write-read round trips.
- `ctest -C Release` 51/51 pass; `cargo fmt/clippy/test` pass.
- Fresh ABI-33 probe recorded in `docs/RESEARCH.md`.
