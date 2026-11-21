# ADR 0176: 64-bit SD/LD MMIO accesses to GS CSR/IMR reach bounded storage (runtime ABI 25)

- **Status:** Accepted
- **Date:** 2026-11-21
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`store_u64` decomposes to `store_u32` for GS CSR/IMR addresses; `load_u64`
  decomposes to `load_u32` for GS CSR/IMR addresses), `tests/gs_csr_smoke.cpp`
  (four new 64-bit MMIO cases),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 24 -> 25),
  `docs/RESEARCH.md` (recorded probe evidence).
- **Related:** ADR 0175 (GS CSR/IMR storage registers, ABI 24), ADR 0180 (loader reply, ABI 23), ADR 0173 (scheduler, ABI 22).

## Context

ADR 0175 modeled GS CSR (`0x1200'1000`) and IMR (`0x1200'1010`) as 32-bit
bounded storage registers, dispatched through `kMmioDevices` only in
`store_u32`/`load_u32`. The real Downhill Domination stop at
`pc=0x002434B4` is a **64-bit** `SD $a0, 0($v1)` (opcode `0xFC64'0000`) to GS
CSR with `$v1 = 0x1200'1000` and `$a0 = 0x200` (bit 9, `wRESET`). The
corresponding IMR read at `pc=0x0024353C` is also a 64-bit `SD`/`LD`.

The generated code lowers a `StoreDoubleword` IR operation to `store_u64`, but
`store_u64` (and `load_u64`) call `resolve_ee_memory_access` directly and write
to `main_ram` — they never touch `kMmioDevices`. So the `sd` to GS CSR hit
`is_unsupported_physical_region(0x1200'1000)` and stopped with
`unsupported_memory_access` instead of writing `gs_csr`. The ABI-24 probe
produced the identical stop PC and register state as the ABI-23 probe
(`pc=0x002434B4`, `$v1=0x1200'1000`, `$a0=0x200`, `unsupported_memory_access`),
proving the ADR 0175 register model never ran.

## Decision

Decompose 64-bit guest accesses to GS CSR/IMR into a single 32-bit MMIO op:

1. **`store_u64(memory, 0x1200'1000 | 0x1200'1010, value)`** writes only the low
   32 bits through `store_u32(memory, address, low)`; the upper 4 bytes of the
   64-bit store are discarded (the register is 32 bits wide).
2. **`load_u64(memory, 0x1200'1000 | 0x1200'1010)`** reads the low 32 bits
   through `load_u32(memory, address)` and zero-extends them to 64 bits.
3. No GS interrupt, no GPU state, no side effects beyond register storage —
   consistent with ADR 0175's "no GPU behind them" boundary.
4. `REPS2_RUNTIME_ABI_VERSION` moves 24 -> 25 because generated code now reaches
   different runtime behavior for `SD`/`LD` to MMIO addresses.

## Consequences

- Downhill ABI-25 probe: the `sd` at `pc=0x002434B4` now writes `gs_csr = 0x200`
  and execution advances past it, into the sound/sound-mixer path following the
  failed bank-load error path (the same path reached by the ABI-23 probe).
- No boot stage is claimed: what the caller expects after writing CSR is still
  unresolved, and REPS2 still models no SPU/sound, GPU rendering, GS DMA, or
  FIFO.
- Existing ABI-24 native modules are refused at admission (`runtime_abi_mismatch`);
  all modules must be rebuilt against ABI 25.

## Validation

- `tests/gs_csr_smoke.cpp`: eight cases — four from ADR 0175 plus
  `gs_csr_store_u64_decomposes`, `gs_csr_load_u64_decomposes`,
  `gs_imr_store_u64_decomposes`, `gs_imr_load_u64_decomposes`.
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` all pass.
- `cmake --build build --config Release --parallel` (MSVC, Windows x64) and
  `ctest -C Release`: 50/50 pass.
- ABI-25 real-title probe `D:\REPS2-DevTests\DownhillDomination\abi25-01`
  recorded in `docs/RESEARCH.md`; no boot stage or Playable claim is made.
