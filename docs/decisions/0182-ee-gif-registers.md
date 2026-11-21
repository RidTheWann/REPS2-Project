# ADR 0182: Bounded GIF control registers GIF_CTRL/GIF_MODE/GIF_STAT (runtime ABI 31)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`kEeGifCtrlAddress`/`kEeGifModeAddress`/`kEeGifStatAddress`,
  `EeMemory::gif_ctrl`/`gif_mode`/`gif_stat`,
  `mmio_read/write_gif_*`, MMIO dispatch entries, 64-bit decomposition),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 30 -> 31),
  `tests/gs_csr_smoke.cpp` (4 GIF cases), `docs/RESEARCH.md`.
- **Related:** ADR 0181 (VIF1 FIFO), ADR 0179 (VIF1 FBRST/ERR),
  ADR 0175-0178 (GS registers), ADR 0180 (VU0 control).

## Context

ABI-30 probe `abi30-01` (main `3414868`, ISO `0fe2b777…a7eb86`, boot ELF
`adfda7b7…f0cf0c`) passed the VIF1 FIFO SQ writes at `pc=0x0024338C`/
`0x00243398` and stopped at `pc=0x0024339C` after 4,645,625 instructions as
`unsupported_memory_access`. Disassembly/register evidence:
`sw $a3, 0($r4)` with `$r4 = 0x10003000`, `$a3 = 1`.

Primary evidence:

- PS2SDK `common/include/ee_regs.h`: `A_EE_GIF_CTRL = 0x10003000`,
  `A_EE_GIF_MODE = 0x10003010`, `A_EE_GIF_STAT = 0x10003020`;
  `WR_EE_GIF_CTRL(x)`/`WR_EE_GIF_MODE(x)`; GIF_STAT has no setter
  (<https://github.com/ps2dev/ps2sdk/blob/master/common/include/ee_regs.h>).
- PS2SDK `common/include/gif_registers.h`: `GIF_REG_CTRL`/`GIF_REG_MODE`/
  `GIF_REG_STAT` at the same addresses; `GIF_SET_CTRL(RST,PSE)` bit 0 = RST,
  bit 3 = PSE; `GIF_SET_MODE(M3R,IMT)` bit 0 = M3R, bit 2 = IMT
  (<https://ps2dev.github.io/ps2sdk/gif__registers_8h_source.html>).
- gsKit `gsInit.h` documents GIF_STAT bit fields (M3R/M3P/IMT/PSE/IP3/P3Q/
  P2Q/P1Q/OPH/APATH/DIR/FQC) and GIF_CTRL reset semantics
  (<https://github.com/ps2dev/gsKit/blob/master/ee/gs/include/gsInit.h>).

## Decision

1. Model `GIF_CTRL` (0x10003000), `GIF_MODE` (0x10003010) and `GIF_STAT`
   (0x10003020) as plain bounded 32-bit storage registers, reset 0, with MMIO
   read/write handlers and dispatch entries in `kMmioDevices`.
2. 64-bit SD/LD to these addresses decompose through `store_u32`/`load_u32`
   (low word only), same pattern as ADR 0176.
3. **No GIF/GS transfer, PATH activity, reset side effect, interrupt, or
   fabricated readiness is modeled.** A guest polling GIF_STAT sees only what
   was written; transfer completion is not claimed.
4. `sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 30 -> 31 and
   ABI-30 modules are refused.

## Consequences

- The ABI-31 probe should pass the GIF_CTRL write at `pc=0x0024339C` and the
  subsequent GIF_MODE/GIF_STAT accesses in the same init sequence.
- If the title polls GIF_STAT for hardware-updated status bits, that behavior
  remains **Unknown** until separately evidenced; this ADR does not fabricate it.
- No boot stage or Playable claim is made.

## Validation

- `tests/gs_csr_smoke.cpp`: GIF_CTRL/GIF_MODE/GIF_STAT storage and GIF_CTRL
  64-bit decomposition.
- `ctest -C Release` 51/51 pass; `cargo fmt/clippy/test` pass.
- Fresh ABI-31 probe recorded in `docs/RESEARCH.md`.
