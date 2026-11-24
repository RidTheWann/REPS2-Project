# ADR 0211: VU0 data memory (VU Mem0) EE MMIO mapping at 0x11004000..0x11004FF0 (no runtime ABI change)

- **Status:** Accepted
- **Date:** 2026-11-24
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp` (MMIO dispatch entry for VU Mem0, read/write handlers), `runtime/include/reps2/runtime/ee_hle.hpp` (registration in `initialize_hle_boot_environment`), `docs/RESEARCH.md`.
- **Related:** ADR 0202 (VU0 data memory storage `Vu0State::data_mem`), ADR 0207 (VILWR/VISWR VI-relative access), ADR 0209 (VADD).

## Context

abi63-01 probe (runtime ABI 41, main `7dda45f` + ADR 0209 VADD) advanced past VADD (`pc=0x002300F8`) to `pc=0x21CBC0` (+1.1K instructions). The stop is `unsupported_memory_access` at `pc=0x21CBC0` on word `0x7C4801C0`: **SQ** (Store Quadword, primary opcode 0x1F, `rs=2`, `rt=8`, `imm=0x1C0`). The effective address is `gpr[2] + 0x1C0 = 0x11004000 + 0x1C0 = 0x110041C0`, which falls in the VU0 data memory window `0x11004000..0x11004FF0` (4 KiB, per Sony VU User's Manual §1.1.5). 

The instruction is **SQ** (Store Quadword), which REPS2 already decodes and lowers via `store_u128` → `store_u128` calls `resolve_ee_memory_access`. The address `0x110041C0` maps to VU Mem0, but the MMIO dispatch table lacks an entry for the VU Mem0 window (`0x11004000..0x11004FF0`), so `resolve_ee_memory_access` falls through and returns `Status::unmapped_memory` → `unsupported_memory_access`.

Evidence: Sony VU User's Manual §1.1.5 (VU Mem 4 KiB for VU0, 128-bit width, qword aligned, address divided by 16 is the word index); PCSX2 `getVUmem(x) = ((isVU1) ? (x & 0x3ff) : ((x >= 0x400) ? (x & 0x43f) : (x & 0xff))) * 16` for VU0 mask 0xFF (4 KiB). EE address map places VU Mem0 at `0x11004000..0x11004FF0` (4 KiB).

## Decision

1. Add `std::uint32_t vu_mem_base` / `vu_mem_size` constants to `EeMemory` (or reuse existing `kEeVuMem0`).
2. Add MMIO dispatch entry for `0x11004000..0x11004FF0` (mask `0xFFF`) that routes to VU Mem0 read/write handlers reusing `vu0_read_vumem_qword`/`vu0_write_vumem_qword` and `store_u128`/`load_u128` (already implemented).
3. Register the MMIO entry in `initialize_hle_boot_environment` (like other MMIO devices).
4. No ABI change: VU Mem0 storage already exists in `Vu0State::data_mem` (ADR 0202), just needs to be addressable via MMIO.

## Consequences

- Frontier should advance past `0x21CBC0` (the SQ store to VU Mem0).
- Enables VU0 data memory access from EE via SQ/LQ/LQC2/SQC2 at the correct EE MMIO addresses.
- No boot/playable claim.

## Validation

- Probe (abi64): should advance past `0x21CBC0` and continue execution.
- CTest 51/51, cargo fmt/clippy/test (406 lib tests).
- Fresh probe (abi64) recorded in `docs/RESEARCH.md`.