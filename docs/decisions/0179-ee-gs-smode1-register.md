# ADR 0179: Model GS SMODE1 (0x12000010) as bounded storage; extend 64-bit decomposition (runtime ABI 28)

- **Status:** Accepted
- **Date:** 2026-11-21
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`kEeGsSmode1Address`, `EeMemory::gs_smode1`,
  `mmio_read/write_gs_smode1`, MMIO dispatch entry,
  64-bit decomposition in `store_u64`/`load_u64`),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 27 -> 28),
  `tests/gs_csr_smoke.cpp` (SMODE1 storage + 64-bit decomposition cases).
- **Related:** ADR 0175 (GS CSR/IMR), ADR 0176 (64-bit MMIO decomposition),
  ADR 0177 (GsGetIMR/GsPutIMR), ADR 0178 (PMODE/SMODE2 + SetGsCrt).

## Context

The ABI-27 probe (`abi27-01`, this branch + ADR 0178, ISO `0fe2b777…a7eb86`,
boot ELF `adfda7b7…f0cf0c`) advanced past the `SetGsCrt` syscall and stopped
at `pc=0x00243318` after 4,645,596 instructions as
`unsupported_memory_access` — a `sw $a3, 0($v0)` where `$v0 = 0x1200'0010`
(SMODE1) and `$a3 = 0x1`. Static disassembly of the CRT-init sequence
shows it immediately follows PMODE (`0x1200'0000`) and SMODE2 (`0x1200'0020`):

```
0x00243310: 0x34423C10  ; ori  $v0, $v0, 0x3C10  # SMODE1 address
0x00243314: 0x3C031000  ; lui  $v1, 0x1000       # PMODE base
0x00243318: 0xAC470000  ; sw   $a3, 0($v0)       # write SMODE1
0x0024331C: 0x34633C20  ; ori  $v1, $v1, 0x3C20  # SMODE2 address
0x00243320: 0x24020002  ; addiu $v0, $zero, 2
0x00243324: 0xAC620000  ; sw   $v0, 0($v1)       # write SMODE2
```

SMODE1 (`0x1200'0010`) is a standard GS CRT register (per PS2TEK/PCSX2
`GSRegs.h`). REPS2 refused the 32-bit `sw` because `0x1200'0010` was not in
the MMIO dispatch table.

## Decision

1. **SMODE1 (`0x1200'0010`)** is added as a plain bounded 32-bit storage
   register `gs_smode1` in `EeMemory`, with MMIO read/write handlers and a
   dispatch entry in `kMmioDevices`.
2. **64-bit decomposition:** `store_u64`/`load_u64` now special-case
   `kEeGsSmode1Address` the same way as PMODE/SMODE2/CSR/IMR — the low 32 bits
   go through `store_u32`/`load_u32`; the upper 4 bytes are discarded.
3. No display output, no vsync, no GPU state fabricated. Guest reads see what
   was written.
4. `sizeof(EeState)` changes (new `gs_smode1` field), so
   `REPS2_RUNTIME_ABI_VERSION` moves 27 -> 28. ABI-27 modules are refused.

## Consequences

- The ABI-28 probe should pass the SMODE1 write at `pc=0x00243318` and
  continue through the SMODE2 write at `pc=0x00243324` (which is now modelled
  via ADR 0178), advancing into whatever follows the CRT-init sequence.
- No boot stage is claimed; REPS2 still models no GPU rendering, GS DMA, or
  display.
- Remaining `0x1200'0000`-page registers (SRFSH, SYNCH*, SYNCV, DISPFB*,
  DISPLAY*, EXT*, BGCOLOR, BUSDIR) stay unmodelled and fail closed with
  `unsupported_memory_access` until individually evidenced.

## Validation

- `tests/gs_csr_smoke.cpp`: added `test_gs_smode1_storage`,
  `test_gs_smode1_store_u64_decomposes`, `test_gs_smode1_load_u64_decomposes`
  (14 cases total).
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` all pass.
- `cmake --build build --config Release --parallel` (MSVC, Windows x64) and
  `ctest -C Release`: 50/50 pass.
- ABI-28 real-title probe `D:\REPS2-DevTests\DownhillDomination\abi28-01`
  recorded in `docs/RESEARCH.md`.