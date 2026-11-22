# ADR 0185: Bounded GIF DMAC channel-2 registers CHCR/MADR/QWC/TADR (runtime ABI 34)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `runtime/include/reps2/runtime/ee_dmac.hpp`
  (`kEeDmacChcr2Address`/`kEeDmacMadr2Address`/`kEeDmacQwc2Address`/
  `kEeDmacTadr2Address`, channel-2 fields/accessors),
  `runtime/include/reps2/runtime/ee_state.hpp` (MMIO dispatch entries and
  `mmio_read/write_*2` handlers),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 33 -> 34),
  `tests/gs_csr_smoke.cpp`, `docs/RESEARCH.md`.
- **Related:** ADR 0184 (VIF0 channel 1), ADR 0182 (GIF control registers),
  ADR 0181 (VIF1 FIFO), ADR 0183 (Timer 1).

## Context

ABI-33 probe `abi33-03` (main `58cc73b`, ISO `0fe2b777…a7eb86`, boot ELF
`adfda7b7…f0cf0c`) passed the channel-1 read at `pc=0x002433D0` after the
reset-zero read fix and stopped at `pc=0x002433D8` after 4,670,452 instructions
as `unsupported_memory_access`. Disassembly/register evidence:
`lw $a1, 0($r9)` with `$r9 = 0x1000A000`.

Primary evidence:

- PS2SDK `common/include/ee_regs.h`:
  `A_EE_D2_CHCR = 0x1000A000`, `A_EE_D2_MADR = 0x1000A010`,
  `A_EE_D2_QWC = 0x1000A020`, `A_EE_D2_TADR = 0x1000A030`,
  plus `RD_EE_D2_*`/`WR_EE_D2_*` accessors
  (<https://github.com/ps2dev/ps2sdk/blob/master/common/include/ee_regs.h>).
- PS2 Developer Wiki memory map: DMAC channel 2 is **GIF**, same four addresses
  (<https://www.psdevwiki.com/ps2/Memory_Map>).
- PS2SDK `dma.c`: `dma_chcr[2] = 0x1000A000` and `dma_channel_wait` polls
  CHCR bit `0x100` (STR), confirming channel-2 CHCR is readable from reset
  state (<https://ps2dev.github.io/ps2sdk/ee_2dma_2src_2dma_8c_source.html>).

## Decision

1. Model channel-2 CHCR/MADR/QWC/TADR as bounded 32-bit storage registers in
   `EeDmacState`, reset 0, always readable, writes read back unchanged.
2. MMIO dispatch entries and `mmio_read/write_*2` handlers route these four
   addresses through the DMAC state.
3. **No DMA transfer, GIF PATH activity, interrupt, STR-clearing on completion,
   or transfer completion is modelled.** Writing CHCR.STR starts nothing here.
4. `sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 33 -> 34 and
   ABI-33 modules are refused.

## Consequences

- The ABI-34 probe should pass the channel-2 read at `pc=0x002433D8` and
  continue into the GIF DMA setup sequence.
- If the title starts a GIF DMA and polls CHCR.STR/D_STAT for completion, that
  remains **Unknown** and must fail visibly, not fabricate completion.
- No boot stage or Playable claim is made.

## Validation

- `tests/gs_csr_smoke.cpp`: `dmac_ch2_registers_storage` covers reset-zero read
  plus CHCR/MADR/QWC/TADR write-read round trips.
- `ctest -C Release` 51/51 pass; `cargo fmt/clippy/test` pass.
- Fresh ABI-34 probe recorded in `docs/RESEARCH.md`.
