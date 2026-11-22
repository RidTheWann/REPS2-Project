# ADR 0188: DMAC channels 1 (VIF0) and 2 (GIF) STR self-clear + D_STAT status bit on start (runtime ABI 37)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `runtime/include/reps2/runtime/ee_dmac.hpp`
  (`kEeDmacDStatVif0StatusBit`, `kEeDmacDStatGifStatusBit`,
  `EeDmacState::write_chcr1/write_chcr2` STR 0->1 completion handling,
  `chcr2_known/madr2_known/qwc2_known/tadr2_known` fields,
  `seed_idle_boot_state` seeds ch1/ch2),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 36 -> 37),
  `tests/dmac_dstat_codegen_smoke.cpp`
  (`test_chcr1_str_sets_dstat_vif0`, `test_chcr2_str_sets_dstat_gif`),
  `docs/RESEARCH.md`.
- **Related:** ADR 0184 (ch1 VIF0), ADR 0185 (ch2 GIF), ADR 0153 (DMAC
  interrupt delivery), ADR 0086 (SIF0 STR completion pattern).

## Context

The ABI-36 probe (`abi36-01`, main `402f850`, ISO `0fe2b777…a7eb86`, boot ELF
`adfda7b7…f0cf0c`) reached `budget_limit_reached` after 79,999,756 instructions
and parked in a spin loop at `pc=0x002433D0` (`0x002433D0..0x00243420`).
Decoded loop:

```
0x002433D0: lw   $v1, ($t2)      ; t2 = 0x10009000 (D1_CHCR, VIF0)
0x002433D8: lw   $a1, ($t1)      ; t1 = 0x1000A000 (D2_CHCR, GIF)
0x002433E0: lw   $a2, ($t0)      ; t0 = 0x10003C00 (VIF1_STAT)
0x00243400: CFC2 $a0, $29        ; VU0 control reg 29 (VPU_STAT)
0x0024340C: lw   $v1, ($a3)      ; a3 = 0x10003020 (GIF_STAT)
0x00243420: bnez $v0, 0x002433D0 ; loop while any condition bit set
```

The loop masks: D1_CHCR bit 8 (STR, 0x100), D2_CHCR bit 8 (STR, 0x100),
VIF1_STAT bits 0..1 (0x3), VU0 CTRL29 bit 8 (0x100), GIF_STAT bits 10..11
(0xC00, active path).

**Observed** guest producers of STR=1 on both channels:

- `0x00216DFC`: `lui $v1,0x1000; ori $v1,$v1,0xE010; addiu $a0,$zero,2;
  sw $a0,($v1)` (D_STAT = 2), `lui $v0,0x1000; ori $v0,$v0,0x9000;
  addiu $v1,$zero,0x145; sw $v1,($v0)` — **D1_CHCR = 0x145** (STR=1, MOD=1
  chain, DIR=1).
- `0x001B573C`: same pattern writes **D2_CHCR = 0x145** (STR=1) then
  `jal 0x002433A8` (the loop entry).
- `0x001EC224`, `0x0021F180`, `0x00224458`: additional D2_CHCR = 0x145/0x105
  writes.

**Derived:** the loop exits only when D1_CHCR.STR == 0, D2_CHCR.STR == 0,
VIF1_STAT[1:0] == 0, VU0 CTRL29 bit 8 == 0, and GIF_STAT[11:10] == 0. Since
REPS2 stored CHCR verbatim, STR stayed 1 forever and the loop spun to the
instruction budget.

Primary evidence for the completion contract:

- PS2Tek DMAC I/O (`https://israpps.github.io/ps2tek/PS2/DMAC/IO.html`):
  `Dn_CHCR` bit 8 = "STR - Start/busy"; the documented polling idiom is
  `while (*CHCR & CHCR_STR) {}` — **STR self-clears on completion**.
- PS2SDK `dma_registers.h`: `DMA_SET_CHCR(..., STR, ...)` places STR at bit 8.
- The same completion semantics were already modelled for channel 5 (SIF0) in
  `write_chcr5` (ADR 0086): STR 0->1 raises D_STAT bit 5 and clears STR.

## Decision

1. `write_chcr1` / `write_chcr2`: on a STR 0->1 transition with D_STAT known,
   raise the channel status bit in D_STAT (bit 1 for ch1/VIF0, bit 2 for ch2/
   GIF) and self-clear STR in the stored CHCR. This mirrors `write_chcr5`.
2. The stored CHCR keeps all other written bits; only STR is cleared.
3. `chcr2/madr2/qwc2/tadr2` gain `*_known` flags (matching ch1/ch5) and are
   seeded by `seed_idle_boot_state` so the HLE boot environment reports them
   as readable idle registers, as the guest already assumes (ADR 0184
   correction).
4. **No data transfer, VIF/GIF FIFO drain, PATH activity, or interrupt
   delivery is modelled.** The D_STAT status bit is raised but the DMAC
   interrupt line is not asserted (D_CTRL.DMAE stays clear in the HLE boot
   environment, and `interrupt_pending()` remains fail-closed).
5. `sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 36 -> 37
   and ABI-36 modules are refused.

## Consequences

- The spin loop at `0x002433D0` should exit once both CHCR STR bits read 0
  (they self-clear immediately on the 0->1 write), assuming VIF1_STAT[1:0],
  VU0 CTRL29 bit 8, and GIF_STAT[11:10] also read 0 (all are storage/reset-0
  or control-register reads returning 0 in the current model).
- If the title then polls other hardware-updated status bits, that behavior
  remains **Unknown** until separately evidenced; this ADR does not fabricate
  VIF/GIF/VU0 readiness.
- No boot stage or Playable claim is made.

## Validation

- `tests/dmac_dstat_codegen_smoke.cpp`: `test_chcr1_str_sets_dstat_vif0` and
  `test_chcr2_str_sets_dstat_gif` assert STR=1 write -> STR reads 0 and the
  matching D_STAT bit is set. Both failed before the fix (exit 112/121),
  pass after.
- `ctest -C Release` 51/51 pass; `cargo fmt --check`, `cargo clippy -D
  warnings`, `cargo test --locked` all pass.
- Fresh ABI-37 probe recorded in `docs/RESEARCH.md`.
