# ADR 0189: DMAC channel 0 (VIF0) registers bounded storage + STR completion (runtime ABI 38)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `runtime/include/reps2/runtime/ee_dmac.hpp`
  (`kEeDmacChcr0Address`..`kEeDmacAsr1Address`, `chcr0/madr0/qwc0/tadr0/asr0/asr1`
  + `*_known` fields, `write_chcr0` STR 0->1 raises D_STAT bit 0 + clears STR,
  `seed_idle_boot_state` seeds all ch0 regs),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 37 -> 38),
  `tests/dmac_dstat_codegen_smoke.cpp` (`test_chcr0_str_sets_dstat_vif0_ch0`),
  `docs/RESEARCH.md`.
- **Related:** ADR 0188 (ch1/ch2 STR completion), ADR 0086 (SIF0 ch5 pattern),
  ADR 0184 (ch1 VIF0), ADR 0185 (ch2 GIF), PS2SDK `ee_regs.h` channel 0,
  `dma.c` channel 0 array entries.

## Context

ABI-37 probe `abi37-01` (main `59b49f4`, ISO `0fe2b777…a7eb86`,
boot ELF `adfda7b7…f0cf0c`) advanced past the ch1/ch2 spin loop at
`0x002433D0` after ADR 0188, but stopped at `pc=0x002181BC` after 4,670,541
instructions as `unsupported_memory_access` on `sw $v1, ($s1)` with
`$s1 = 0x10008020` (D0_QWC, DMAC channel 0 quad-word count) and `$v1 = 3`.

**Primary evidence for channel 0:**

- PS2SDK `common/include/ee_regs.h` defines VIF0 (channel 0) registers:
  `A_EE_D0_CHCR = 0x10008000`, `A_EE_D0_MADR = 0x10008010`,
  `A_EE_D0_QWC = 0x10008020`, `A_EE_D0_TADR = 0x10008030`,
  `A_EE_D0_ASR0 = 0x10008040`, `A_EE_D0_ASR1 = 0x10008050`.
- PS2SDK `ee/dma/src/dma.c` channel array index 0 uses these exact addresses
  for `dma_chcr[0]`, `dma_madr[0]`, `dma_qwc[0]`, `dma_tadr[0]`.
- The guest code at `0x002181A4`..`0x002181BC` initializes `QWC=3`
  into D0_QWC (`$s1 = 0x10008020`, `$v1 = 3`) as part of a VIF0 DMA
  setup sequence, then calls the same spin loop at `0x002433A8`.

**Derived:** channel 0 is the VIF0 DMA path (same as channel 1 is also VIF0
per some SDK variants; ps2sdk `ee_regs.h` lists both D0_* and D1_* for
VIF0). The title uses both channel 0 and channel 1 for VIF0 operations.
The spin loop polls both STR bits (and channel 2 STR). With ch1/ch2
STR now self-clearing (ADR 0188), the next unmodelled register is D0_QWC.

## Decision

1. Model DMAC channel 0 (VIF0) registers as bounded storage:
   `CHCR (0x10008000)`, `MADR (0x10008010)`, `QWC (0x10008020)`,
   `TADR (0x10008030)`, `ASR0 (0x10008040)`, `ASR1 (0x10008050)`.
2. `write_chcr0`: on STR 0->1 transition with D_STAT known, raise
   D_STAT bit 0 and self-clear STR (mirrors ch1/ch2/ch5).
3. All ch0 registers get `*_known` flags; `seed_idle_boot_state` seeds
   them to 0 with known=true so guest reads return 0 before first write.
4. **No DMA transfer, VIF0 FIFO, PATH activity, or interrupt delivery
   is modelled.** The STR completion is purely local: STR bit cleared,
   D_STAT bit raised. Interrupt line stays unasserted because D_CTRL.DMAE
   remains clear in HLE boot environment.
5. `sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 37 -> 38
   and ABI-37 modules are refused.

## Consequences

- The VIF0 DMA setup at `0x002181A0`..`0x002181BC` should succeed.
- The spin loop at `0x002433D0` already polls ch0 STR? No, it polls
  ch1 (t2=0x10009000) and ch2 (t1=0x1000A000). Channel 0 STR is not
  currently polled in that loop, but the DMA setup writes to D0_QWC,
  which now resolves.
- If the title then polls D0_CHCR STR or other hardware-updated bits,
  that behavior remains **Unknown** until separately evidenced; this ADR
  does not fabricate VIF0 transfer completion or FIFO state.
- No boot stage or Playable claim is made.

## Validation

- `tests/dmac_dstat_codegen_smoke.cpp`: `test_chcr0_str_sets_dstat_vif0_ch0`
  asserts STR=1 write -> STR reads 0 and D_STAT bit 0 is set.
- `ctest -C Release` all pass; `cargo fmt --check`, `cargo clippy -D
  warnings`, `cargo test --locked` all pass.
- Fresh ABI-38 probe to be recorded in `docs/RESEARCH.md`.