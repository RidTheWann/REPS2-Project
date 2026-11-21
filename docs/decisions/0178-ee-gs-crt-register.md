# ADR 0178: Model GS CRT registers PMODE (0x12000000) / SMODE2 (0x12000020); serve SetGsCrt syscall (0x02) (runtime ABI 27)

- **Status:** Accepted (validated locally; real-title probe recorded)
- **Date:** 2026-11-21
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`kEeGsPmodeAddress`/`kEeGsSmode2Address`, `EeMemory::gs_pmode`/`gs_smode2`,
  `mmio_read/write_gs_pmode`/`mmio_read/write_gs_smode2`, MMIO dispatch entries,
  64-bit decomposition extension in `store_u64`/`load_u64`),
  `runtime/include/reps2/runtime/ee_hle.hpp`
  (`kEeSyscallSetGsCrt`, `hle_set_gs_crt`, service-table entry),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 26 -> 27),
  `tests/gs_csr_smoke.cpp` (3 new cases: PMODE storage, SMODE2 storage,
  PMODE 64-bit decomposition), `tests/hle_syscall_smoke.cpp` (SetGsCrt
  registration assertion).
- **Related:** ADR 0175 (GS CSR/IMR), ADR 0176 (64-bit MMIO decomposition),
  ADR 0177 (GsGetIMR/GsPutIMR syscalls).

## Context

The ABI-26 probe (`abi26-01`, commit `9531e98`, ISO `0fe2b777…a7eb86`, boot
ELF `adfda7b7…f0cf0c`) advanced past the `GsPutIMR` stop and ended
`guest_stopped` at `pc=0x0025A2E4` after 4,645,581 instructions as
`unhandled_syscall` for service `0x02`. Static disassembly of the stub at
`0x0025A2E0`: `addiu $v1,$zero,2 ; syscall` — `SetGsCrt($a0,$a1,$a2)`. Per
ps2sdk `ee/kernel/include/syscallnr.h`, `__NR_SetGsCrt = 2`, and per
`ee/kernel/include/kernel.h` the signature is
`void SetGsCrt(s16 interlace, s16 pal_ntsc, s16 field)`.

The GS CRT/video-mode registers live in the `0x1200'0000` page (PS2TEK/PCSX2
`GSRegs.h` agree on the map): PMODE `0x1200'0000`, SMODE1 `0x1200'0010`,
SMODE2 `0x1200'0020`. REPS2 refused every physical address in
`0x1000'0000..0x1FFF'FFFF` not covered by a modelled MMIO device.

## Decision

1. **PMODE (`0x1200'0000`) and SMODE2 (`0x1200'0020`)** are modelled as plain
   bounded 32-bit storage registers (`gs_pmode`/`gs_smode2`, reset 0), with
   MMIO dispatch entries and 64-bit `SD`/`LD` decomposition through
   `store_u32`/`load_u32` (same pattern as ADR 0176).
2. **SetGsCrt (0x02)** is registered in `kEeSyscallServices`. The handler reads
   `$a0`/`$a1`/`$a2` (interlace, pal_ntsc, field), composes a SMODE2 value
   (`bit0=interlace`, `bit1=field`, `bit4=pal_ntsc` per PS2TEK SMODE2 layout),
   writes it to SMODE2, writes a fixed PMODE value `0x3` (EN1|EN2 output
   enable), and returns 0 in `$v0`.
3. **No display output, no vsync, no GPU state** is fabricated. If the guest
   later reads SMODE2/PMODE it sees exactly what was written — that is the
   guest-visible contract.
4. The exact bit composition of SMODE2 from the three arguments is classified
   **Inferred** (derived from PS2TEK bit layout + ps2sdk signature, not yet
   cross-verified against shipped hardware scope trace). If a future probe
   shows the guest reading SMODE2 expecting different bits, the composition
   must be corrected at this handler — not special-cased downstream.
5. `sizeof(EeState)` changes (`gs_pmode`/`gs_smode2` added), so
   `REPS2_RUNTIME_ABI_VERSION` moves 26 -> 27. ABI-26 modules are refused.

## Consequences

- The ABI-27 probe should pass the `SetGsCrt` stop at `pc=0x0025A2E4` and
  advance into whatever the title does next (likely further GS register setup
  or a return to the sound-init path).
- No boot stage is claimed; REPS2 still models no GPU rendering, GS DMA, or
  display.
- Remaining `0x1200'0000`-page registers (SMODE1, SRFSH, SYNCH*, SYNCV, DISPFB*,
  DISPLAY*, EXT*, BGCOLOR, BUSDIR) stay unmodelled and fail closed with
  `unsupported_memory_access` until individually evidenced.

## Validation

- `tests/gs_csr_smoke.cpp`: 11 cases total (8 from ADR 0175/0176 + PMODE
  storage, SMODE2 storage, PMODE 64-bit decomposition).
- `tests/hle_syscall_smoke.cpp`: asserts `find_ee_syscall_service(kEeSyscallSetGsCrt)`
  non-null and name `"SetGsCrt"`.
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` all pass.
- `cmake --build build --config Release --parallel` (MSVC, Windows x64) and
  `ctest -C Release`: 50/50 pass.
- ABI-27 real-title probe `D:\REPS2-DevTests\DownhillDomination\abi27-01`
  recorded in `docs/RESEARCH.md`.
