# ADR 0130: Normalize KSEG0/KSEG1 aliases before EE MMIO dispatch

- **Status:** Accepted.
- **Date:** 2026-11-11.
- **Scope:** EE 32-bit MMIO load/store address selection.

## Context

The EE runtime supports physical register addresses in its `EeMemory::kMmioDevices` table. Main-RAM access resolution already recognizes direct-mapped KSEG0/KSEG1 aliases, but `load_u32` and `store_u32` previously searched the MMIO table using the guest virtual address verbatim. A guest access through an uncached KSEG1 alias therefore missed a modeled device register and fell through to the generic unsupported-physical-region path.

The current Downhill investigation exposes the same address form: the GPR snapshot records `0xFFFFFFFFB0001800`, whose low 32-bit address is the KSEG1 alias of physical `0x10001800` (`T3_COUNT`). The Count register remains unsupported; this ADR addresses the general MMIO lookup path, not the Timer 3 counter implementation.

## Evidence

- ADR 0018 and the memory access contract define direct-mapped KSEG0/KSEG1 addresses as aliases of the same low-29-bit physical address.
- `resolve_ee_memory_access` already normalizes addresses in `0x80000000..0xBFFFFFFF` with `address & 0x1FFFFFFF` when classifying unsupported physical regions.
- PS2SDK exposes the EE Timer 3 mode register at physical `0x10001810`; the KSEG1 alias is `0xB0001810`. The Timer 3 mode device exists in the MMIO table from ADR 0073.
- Before this change, MMIO lookup compared `0xB0001810` directly with `0x10001810`, so the existing mode-only device could not be selected through that alias.

## Decision

1. Normalize direct-mapped KSEG0/KSEG1 guest addresses to their low-29-bit physical address before matching the existing MMIO device table in both `load_u32` and `store_u32`.
2. Continue passing only the matched physical register address to the device handler.
3. Preserve exact physical-address access behavior.
4. Do not add TLB mapping, cache/coherency semantics, new MMIO devices, or timer behavior. In particular, `T3_COUNT` and `T3_COMP` remain unsupported and fail closed.

## Consequences

- Modeled MMIO registers can be reached through their direct-mapped KSEG0/KSEG1 aliases as well as their physical address.
- The existing Timer 3 mode-only model becomes reachable via `0xB0001810`; no counter progression or compare/interrupt behavior is implied.
- The observed `0xB0001800` Count access remains an explicit `unsupported_memory_access` until a separately validated Timer 3 model is implemented.
- This normalization changes address selection only. It does not simulate KSEG0 cache effects.

## Validation

- Regression tests read/write Timer 3 mode through physical, KSEG1 (`0xB0001810`) and KSEG0 (`0x90001810`) addresses and confirm the aliases share one register state.
- Regression tests verify physical and KSEG1 Count/Compare reads and writes remain unsupported and do not synthesize register values.
- The PR CI validates Rust formatting/lints/tests, CMake build/CTest on Linux and Windows/MSVC, plus the Windows install-tree and developer-diagnostics gates.
