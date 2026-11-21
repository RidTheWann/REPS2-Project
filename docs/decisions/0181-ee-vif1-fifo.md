# ADR 0181: Bounded VIF1 FIFO port (0x10005000) with qword push/pop (runtime ABI 30)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`kEeVif1FifoAddress`, `kEeVif1FifoCapacityQwords`, `EeMemory::vif1_fifo`/
  `vif1_fifo_head`/`vif1_fifo_count`, `vif1_fifo_push`/`vif1_fifo_pop`,
  dispatch inside `store_u128`/`load_u128`),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 29 -> 30),
  `tests/vif1_fifo_smoke.cpp`, `CMakeLists.txt` test registration,
  `docs/RESEARCH.md`.
- **Related:** ADR 0179 (VIF1 FBRST/ERR), ADR 0180 (VU0 CFC2/CTC2),
  ADR 0175-0178 (GS/VIF register storage).

## Context

ABI-29 probes (`abi29-02`..`abi29-07`, main `5666883`, ISO
`0fe2b777…a7eb86`, boot ELF `adfda7b7…f0cf0c`) consistently stopped at
`pc=0x0024338C` after 4,645,625 instructions as `unsupported_memory_access`.
Disassembly/register evidence: `SQ $a2, 0($r5)` with `$r5 = 0x10005000`.

Primary evidence:

- Sony **EE User's Manual v6.0**, register map: "FIFO’s are 128-bit long and
  only qword-accessible"; `0x1000_5000` is listed as **VIF1 FIFO (read/write)**
  (<https://docs.alexrp.com/mips/ee.pdf>).
- PS2SDK `common/include/ee_regs.h`: `A_EE_VIF1_FIFO = 0x10005000`,
  `R_EE_VIF1_FIFO = (vu32*)A_EE_VIF1_FIFO`, `WR_EE_VIF1_FIFO(x)`
  (<https://github.com/ps2dev/ps2sdk/blob/master/common/include/ee_regs.h>).
- PS2Tek VIF I/O register reference: VIF1 FIFO capacity is 16 quadwords; FQC in
  VIF1_STAT bits 24..28 reports queued quadwords
  (<https://israpps.github.io/ps2tek/PS2/VIF/IO_Regs.html>).
- PCSX2 behavioral cross-check only: `_hwWrite128` page 0x05 routes to
  `WriteFIFO_VIF1`, `_hwRead128` routes to `ReadFIFO_VIF1`, and reads assert
  `FQC != 0`; writes push qwords into the 16-entry FIFO. Not normative.

## Decision

1. Model `0x10005000` as a **bounded 16-qword ring FIFO** in `EeMemory`, not as
   RAM and not as an unbounded sink.
2. `store_u128` (SQ) pushes one qword; a full FIFO returns
   `unsupported_memory_access` and mutates nothing.
3. `load_u128` (LQ) pops the oldest qword; an empty FIFO returns
   `unsupported_memory_access` and leaves the caller output unchanged.
4. KSEG0/KSEG1 aliases normalize through `normalize_ee_mmio_address`; only
   16-byte-aligned addresses reach the FIFO, otherwise `address_error`.
5. No VIF command parser, DMA transfer, GIF path, VU execution, or FQC status
   side effect is claimed. Occupancy is stored state only.
6. `sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 29 -> 30 and
   ABI-29 modules are refused.

## Consequences

- The ABI-30 probe should pass the `SQ` at `pc=0x0024338C` and continue into the
  next VIF/GIF init sequence.
- If the guest immediately reads VIF1_STAT.FQC expecting hardware-updated
  occupancy, that status behavior is **Unknown** until separately evidenced;
  this ADR does not fabricate it.
- No boot stage or Playable claim is made.

## Validation

- `tests/vif1_fifo_smoke.cpp`: empty-read fail-closed, single push/pop,
  16-entry wrap ordering, full-write refusal without mutation.
- `cargo fmt/clippy/test` and `ctest -C Release` (51/51) pass.
- Fresh ABI-30 probe recorded in `docs/RESEARCH.md`.
