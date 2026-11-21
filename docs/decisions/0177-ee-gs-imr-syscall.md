# ADR 0177: Serve GsGetIMR (0x70) / GsPutIMR (0x71) syscalls over the GS IMR register (runtime ABI 26)

- **Status:** Accepted
- **Date:** 2026-11-21
- **Scope:** `runtime/include/reps2/runtime/ee_hle.hpp`
  (`kEeSyscallGsGetImr`/`kEeSyscallGsPutImr` constants, `hle_gs_get_imr`/
  `hle_gs_put_imr` handlers, two new `kEeSyscallServices` entries),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 25 -> 26),
  `tests/hle_syscall_smoke.cpp` (table-registration assertions).
- **Related:** ADR 0175 (GS CSR/IMR storage registers), ADR 0176 (64-bit MMIO
  decomposition, ABI 25), ADR 0069 (guest syscall handlers).

## Context

The ABI-25 probe (`D:\REPS2-DevTests\DownhillDomination\abi25-01`, commit
`0226a34` + ADR 0176 fix, ISO `0fe2b777…a7eb86`, boot ELF `adfda7b7…f0cf0c`)
advanced past the GS CSR write at `pc=0x002434B4` (the `sd $a0, 0($v1)` with
`$v1=0x1200'1000`, `$a0=0x200`) and stopped at a new frontier
`pc=0x0025AA24` after 4,645,563 instructions: `unhandled_syscall` for service
`0x71` (113 decimal, refused=1 in the census). Static disassembly of the boot
RAM image shows the stub at `0x0025AA20` is `addiu $v1, $zero, 0x71 ; syscall ;
jr $ra ; nop`, i.e. `GsPutIMR($a0)`. Per ps2sdk `ee/kernel/include/syscallnr.h`,
`__NR_GsGetIMR = 0x70` and `__NR_GsPutIMR = 0x71`; the kernel routines read and
write the GS interrupt mask register (IMR at `0x1200'1010`).

ADR 0175 already models IMR as a bounded 16-bit storage register at
`0x1200'1010` with MMIO dispatch, but only through `store_u32`/`load_u32` —
the EE syscall path (`kEeSyscallServices`) had no entry for 0x70/0x71, so
`dispatch_ee_syscall` returned `unhandled_syscall`.

## Decision

Register both services in `kEeSyscallServices`:

1. **`GsGetIMR` (0x70):** `hle_gs_get_imr` loads the IMR register through
   `load_u32(state.memory, kEeGsImrAddress, value)` (the same MMIO path as a
   guest `lw`), returning the 16-bit mask zero-extended in `$v0`.
2. **`GsPutIMR` (0x71):** `hle_gs_put_imr` stores `$a0 & 0xFFFF` into the IMR
   register through `store_u32(state.memory, kEeGsImrAddress, value)` (the same
   MMIO path as a guest `sw`), returning 0 in `$v0` on success.
3. No GS interrupt is ever delivered; no GPU state is fabricated (consistent
   with ADR 0175's "no GPU behind them" boundary).
4. `REPS2_RUNTIME_ABI_VERSION` moves 25 -> 26 because the service table is part
   of the generated-code-visible dispatch contract.

## Consequences

- The ABI-26 probe should pass the `GsPutIMR` stop at `0x0025AA24` and advance
  into whatever the title does after writing IMR (likely GsGetIMR or further
  GS CSR polling).
- No boot stage is claimed; REPS2 still models no GPU rendering, GS DMA, or
  interrupt delivery.
- ABI-25 modules are refused at admission; all modules must be rebuilt.

## Validation

- `tests/hle_syscall_smoke.cpp`: `test_service_table_reports_names` now asserts
  `find_ee_syscall_service(kEeSyscallGsGetImr)` and
  `find_ee_syscall_service(kEeSyscallGsPutImr)` are non-null and the name is
  `"GsPutIMR"`.
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` all pass.
- `cmake --build build --config Release --parallel` (MSVC, Windows x64) and
  `ctest -C Release`: 50/50 pass.
- ABI-26 real-title probe recorded in `docs/RESEARCH.md`.
