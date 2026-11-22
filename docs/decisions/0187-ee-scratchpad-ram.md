# ADR 0187: Bounded EE Scratchpad RAM (SPR) 0x70000000..0x70003FFF (runtime ABI 36)

- **Status:** Accepted
- **Date:** 2026-11-22
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`EeMemory::scratchpad`, `kEeScratchpadBaseAddress`, `kEeScratchpadSize`,
  `scratchpad_offset`, `scratchpad_read/write_u8/u16/u32/u64/u128`,
  dispatch in `load_u*/store_u*`, removal of SPR from
  `is_unsupported_physical_region`),
  `runtime/include/reps2/runtime/abi_version.hpp` (ABI 35 -> 36),
  `tests/gs_csr_smoke.cpp`, `tests/control_flow_smoke.cpp`,
  `docs/RESEARCH.md`.
- **Related:** ADR 0186 (VIF1_STAT), ADR 0185 (GIF DMAC ch2), ADR 0184 (VIF0 DMAC ch1),
  ADR 0183 (Timer 1), ADR 0181 (VIF1 FIFO).

## Context

ABI-35 probes `abi35-01`/`abi35-02` (main `97d5caa`, ISO `0fe2b777…a7eb86`,
boot ELF `adfda7b7…f0cf0c`) advanced past VIF1_STAT and stopped at
`pc=0x00218118` after 4,670,492 instructions as `unsupported_memory_access`.
Disassembly/register evidence: `sw $a2, 0x2c($s0)` with `$s0 = 0x70000000`,
`$a2 = 0x800000`.

Primary evidence:

- Sony **EE User's Manual v6.0**, EE Core configuration: the EE core contains a
  **16 KB Data Scratchpad (SPR)**; PS2Tek/PS2 Developer Wiki map it to
  physical `0x70000000..0x70003FFF`
  (<https://docs.alexrp.com/mips/ee.pdf>, <https://www.psdevwiki.com/ps2/Memory_Map>).
- PS2SDK `common/include/ee_regs.h` places DMAC SPR channel registers at
  `0x10008080`/`0x10009080`/`0x1000D080`, confirming the SPR physical window.

## Decision

1. Model the SPR as a **bounded 16 KiB byte array** inside `EeMemory`
   (`std::array<std::uint8_t, 16384> scratchpad`), reset zero.
2. `0x70000000..0x70003FFF` is removed from `is_unsupported_physical_region`.
3. All standard load/store widths route SPR addresses through
   `scratchpad_read/write_u8/u16/u32/u64/u128` before main RAM, with
   width/alignment checks: misaligned access returns `address_error`, and
   out-of-window access returns `unmapped_memory`.
4. Little-endian byte assembly matches the rest of the memory model.
5. **No DMA, MFIFO ring, SPR-channel aliasing, or cache behavior is modelled.**
   SPR is plain RAM only.
6. `sizeof(EeState)` changes, so `REPS2_RUNTIME_ABI_VERSION` moves 35 -> 36 and
   ABI-35 modules are refused.

## Consequences

- The ABI-36 probe should pass the SPR `sw` at `pc=0x00218118` and continue
  into the next init sequence.
- If the title starts SPR DMA or polls DMAC completion, that remains
  **Unknown** and must fail visibly, not fabricate transfer completion.
- No boot stage or Playable claim is made.

## Validation

- `tests/gs_csr_smoke.cpp`: `scratchpad_accesses` covers reset-zero read,
  word/doubleword round trips, last-word boundary, misaligned address error,
  and out-of-window unmapped.
- `tests/control_flow_smoke.cpp`: fixed a stack-allocated `EeState` that had
  no main-RAM backing, exposed only after the SPR array enlarged `EeState`.
- `ctest -C Release` 51/51 pass; `cargo fmt/clippy/test` pass.
- Fresh ABI-36 probe recorded in `docs/RESEARCH.md`.
