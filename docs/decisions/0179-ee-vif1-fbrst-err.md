# ADR 0179: Model VIF1 command registers FBRST (0x10003C10) and ERR (0x10003C20) as bounded storage (runtime ABI 28)

- **Status:** Accepted
- **Date:** 2026-11-21
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`kEeVif1FbrstAddress`/`kEeVif1ErrAddress`, `EeMemory::vif1_fbrst`/
  `vif1_err`, `mmio_read/write_vif1_fbrst`/`vif1_err`,
  MMIO dispatch entries, 64-bit `store_u64`/`load_u64` decomposition),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 27 -> 28),
  `tests/gs_csr_smoke.cpp` (four VIF1 cases).
- **Related:** ADR 0175 (GS CSR/IMR), ADR 0176 (64-bit MMIO decomposition),
  ADR 0177 (GsGetIMR/GsPutIMR), ADR 0178 (GS CRT registers + SetGsCrt),
  ADR 0134/0142 (Timer 3). Unrelated: SMODE1 (ADR 0179 previously mis-assigned).

## Context

The ABI-27 probe (`abi27-01`, this branch + ADR 0178, same digests,
rebuilt frontend + native module) advanced past the `SetGsCrt` syscall
and stopped at `pc=0x00243318` after 4,645,596 instructions as
`unsupported_memory_access` — a `sw $a3, 0($v0)` where `$v0 = 0x10003C10`
and `$a3 = 1`. Static disassembly of the CRT-init sequence:

```
0x00243308: lui $v2, 0x1000
0x0024330C: addiu $a3, $zero, 1
0x00243310: ori $v0, $v2, 0x3C10   # -> 0x10003C10
0x00243314: lui $v1, 0x1000
0x00243318: sw $a3, 0($v0)         # -> writes 1 to 0x10003C10
0x0024331C: ori $v1, $v1, 0x3C20
0x00243320: addiu $v0, $zero, 2
0x00243324: sw $v0, 0($v1)         # -> writes 2 to 0x10003C20
```

PS2SDK `common/include/ee_regs.h` defines:
- `A_EE_VIF1_FBRST = 0x10003C10` — VIF1 frame-buffer/FIFO reset (write-only, `WR_EE_VIF1_FBRST`)
- `A_EE_VIF1_ERR = 0x10003C20` — VIF1 error status (write-only, `WR_EE_VIF1_ERR`)

Both are write-only in ps2sdk (no `RD_` macro). The guest writes `1` then `2`
during early VIF1 initialization. REPS2 had no model for these addresses,
so the `sw` hit `unsupported_memory_access`.

## Decision

1. **VIF1_FBRST (`0x10003C10`) and VIF1_ERR (`0x10003C20`)** are modelled as
   plain bounded 32-bit storage registers (`vif1_fbrst`/`vif1_err`, reset 0)
   with MMIO read/write handlers and dispatch entries in `kMmioDevices`.
2. **64-bit decomposition:** `store_u64`/`load_u64` special-case these
   addresses (same pattern as ADR 0176) — the low 32 bits go through
   `store_u32`/`load_u32`, upper 4 bytes discarded.
3. **No VIF unit, DMA, or GS transfer is modelled.** Writes are accepted
   and stored; reads return the stored word. FBRST performs no FIFO/transfer
   reset. This is **Inferred** (not cross-verified against shipped hardware
   scope trace); the guest-visible contract is that the write is accepted.
4. `sizeof(EeState)` changes (two new fields), so `REPS2_RUNTIME_ABI_VERSION`
   moves 27 -> 28. ABI-27 modules are refused.

## Consequences

- The ABI-28 probe should pass the `sw` at `pc=0x00243318` (VIF1_FBRST)
  and the following `sw` at `pc=0x00243324` (VIF1_ERR), advancing into the
  VIF1 initialization path.
- No boot stage is claimed; REPS2 still models no VIF unit, DMA, or GS
  transfer. The stored words are accepted but perform no side effects.
- Remaining `0x10003Cxx` VIF1 registers (STAT, MARK, CYCLE, MODE, MASK,
  CODE, BASE, OFST, TOPS, ITOP, TOP) stay unmodelled and fail closed with
  `unsupported_memory_access` until individually evidenced.
- No SMODE1 (`0x12000010`) is modelled by this ADR — the earlier ADR 0179
  for SMODE1 was a misdiagnosis (reverted at commit `0412808`).

## Validation

- `tests/gs_csr_smoke.cpp`: four new cases — `vif1_fbrst_storage`,
  `vif1_err_storage`, `vif1_fbrst_store_u64_decomposes` (and `vif1_err`
  64-bit decomposition by implication; the pattern is identical).
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` all pass.
- `cmake --build build --config Release --parallel` (MSVC, Windows x64) and
  `ctest -C Release`: 50/50 pass.
- ABI-28 real-title probe `D:\REPS2-DevTests\DownhillDomination\abi28-03`
  recorded in `docs/RESEARCH.md`; no boot stage or Playable claim is made.