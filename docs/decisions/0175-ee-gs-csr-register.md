# ADR 0175: Model GS CSR (0x12001000) / IMR (0x12001010) bounded storage registers (runtime ABI 24)

- **Status:** Accepted (validated locally; see Validation).
- **Date:** 2026-11-21.
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`kEeGsCsrAddress`/`kEeGsImrAddress`, `EeMemory::gs_csr`/`EeMemory::gs_imr`,
  `mmio_read_gs_csr`/`mmio_write_gs_csr`/`mmio_read_gs_imr`/`mmio_write_gs_imr`,
  MMIO dispatch entries), `runtime/include/reps2/runtime/abi_version.hpp`
  (ABI 23 -> 24), `engine/src/main.cpp` (run-report `gs_*` fields),
  `tests/gs_csr_smoke.cpp` (four MMIO cases).
- **Related:** ADR 0073 (Timer 3 register model pattern), ADR 0174 (loader reply),
  ADR 0173 (scheduler, ABI 22).

## Context

The ABI-23 probe (`abi23-01`/`abi23-02`, reps2 commit `0f75405`, ISO
`0fe2b777…a7eb86`, boot ELF `adfda7b7…f0cf0c`) advanced from
`budget_limit_reached @ pc=0x00201888` (79,999,980 instr, 6,854,902 `FlushCache`)
to `guest_stopped @ pc=0x002434B4` after 4,645,561 instructions
(`syscalls_seen=1239 handled=1222`), reproducible across two runs.

Static disassembly of `0x002434B4` (boot RAM image, file offset == guest VA):

```
0x002434B4: .word 0xFC64'0000     ; sd $a0, 0($v1)
```

with `$a0 = 0x200` and `$v1 = 0x1200'1000` at stop. This is a 64-bit store
(`SD`, opcode `0x3F`) of value `0x200` to EE address `0x1200'1000` — the
**GS CSR** (privileged Graphics Synthesizer control/status register). REPS2
returns `unsupported_memory_access` for every physical address in
`0x1000'0000..0x1FFF'FFFF` that is not a modelled MMIO device, so the write
to `0x1200'1000` stops execution on `unsupported_memory_access`.

The guest's preceding sequence arms GS CSR bit 9 (`wRESET`): `lui $v0, 0x1200`,
`ori $v0, $v0, 0x1000`, `sd $a0, 0($v0)` with `$a0 = 0x200`. It then reads
`0x1200'1010` (GS IMR) at `0x0024353C`. Per PS2TEK, bit 9 of CSR is `wRESET`
(write-reset), bit 8 is `wFLUSH`; the low 16 bits are read-only status
(SIGNAL/FINISH/HSINT/VSINT/EDWINT/FLUSH/RESET/NFIELD/FIELD/FIFO/REV/ID) and
the high 16 bits are write-1 command bits that mirror them.

## Decision

Model GS CSR/IMR as **bounded storage registers with no GPU behind them**, on
the same pattern as ADR 0073 (Timer 3: deterministic storage, no interrupt):

1. **CSR (0x1200'1000):** `store_u32` writes the full command word into
   `EeMemory::gs_csr`. `load_u32` returns `gs_csr & 0xFFFF'0000U` — the command
   word is preserved, but the low 16 status bits are always clear because
   REPS2 models no GPU and therefore never raises SIGNAL/FINISH/HSINT/VSINT/
   EDWINT/FLUSH/RESET. Reset 0.
2. **IMR (0x1200'1010):** `store_u32` stores `value & 0xFFFF` into
   `EeMemory::gs_imr`. `load_u32` returns `gs_imr & 0xFFFF` (plain 16-bit
   mask). Reset 0.
3. **No GS interrupt is ever delivered**; the status half of CSR reads as 0.
4. **KSEG0/KSEG1 aliases** (`0x8000'0000`/`0xA000'0000` mapped to `0x0000'0000`..`0x1FFF'FFFF`) route through `store_u32`/`load_u32` normalization to the same MMIO entry, so both `0x1200'1000` and `0x9200'1000` hit CSR.
5. **No GPU state is fabricated**: no rendering, no display, no GS DMA, no
   FIFO, no PRIM/SPRITE state, no reset side effects beyond register storage.
   The runtime boundary stays honest: writing `0x200` (wRESET) to CSR records
   the value but performs no GPU reset.

`sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 23 -> 24 and
every generated module must be rebuilt. ABI-23 modules are refused.

## Consequences

- The run report gains `gs_*` fields (csr value, imr value).
- The `sd` to `0x1200'1000` now succeeds and execution advances past
  `pc=0x002434B4`, past the subsequent GS IMR read at `0x0024353C`, into the
  sound/sound-mixer path the title enters after the GS reset attempt.
- No boot stage is claimed: this only removes a stop on an unsupported MMIO
  register. What `0x002434B4`'s caller expects after writing CSR is unresolved
  (the title's own failed-bank-load error path runs next).
- REPS2 still does not model SPU/sound hardware, GPU rendering, GS DMA, or any
  I/O beyond register storage.

## Validation

- `tests/gs_csr_smoke.cpp`: four cases — `gs_csr_reset_write_is_not_unsupported`
  (wRESET write accepted), `gs_csr_read_clears_status_half` (read returns high
  16 only), `gs_imr_is_16bit_mask` (plain 16-bit storage, reset 0),
  `gs_csr_alias_dispatches` (KSEG0 alias hits same register).
- `cmake --build build --config Release --parallel` (MSVC, Windows x64) and
  `ctest -C Release`: 50/50 pass (incl. new GS test).
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` all pass (no Rust changes).

## Real-title result

ABI-24 probe `D:\REPS2-DevTests\DownhillDomination\abi24-01` (this branch's
headers, rebuilt frontend + native module, same ISO/ELF digests, same extra root
`0x00246CD0`, both relocated regions, 5,000,000-instruction budget, 16 batches).
See `docs/RESEARCH.md` for the recorded outcome; no boot stage or Playable claim
is made by this ADR.