# ADR 0024: Coalesce Pure BSS PT_LOAD Segments for SN Systems ProDG Compatibility

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** ELF program header loader (`rust/frontend/src/elf.rs`), memory safety validation, retail game executable recompiler pipeline

## Context

During retail ELF recompiler validation across the local PS2 library (`D:\List Games`), titles linked with the SN Systems ProDG toolchain (including Rockstar Games titles such as `Bully (USA)` and `Grand Theft Auto - San Andreas`) failed to recompile with the following loader error:

```
ELF PT_LOAD virtual memory ranges overlap: [0x25cf00..0x33e5c0) and [0x25cf00..0x33e5c0)
```

Inspection of the ELF headers for these titles revealed that the ProDG linker generates multiple consecutive `PT_LOAD` program headers targeting the exact same virtual base address (`_fbss`), each having `p_filesz == 0` (pure zero-filled BSS) but describing different section extents or overlay boundaries. In addition, ProDG-linked binaries frequently include empty terminator `PT_LOAD` headers where both `p_filesz == 0` and `p_memsz == 0`.

The original REPS2 loader tracked virtual memory intervals `[virtual_address, virtual_address + memory_size)` across all `PT_LOAD` headers, rejecting any overlap as fatal. While correct for initialized data segments with file bytes (`p_filesz > 0`), rejecting overlapping zero-filled segments prevented real commercial PS2 executables from loading.

## Decision

**Update `load_segments` in `rust/frontend/src/elf.rs` to skip empty segments and coalesce pure BSS segments sharing identical base virtual addresses:**

1. **Empty Segment Skipping:**
   - Any `PT_LOAD` header with `memory_size == 0` is safely skipped. It contributes no bytes, requires no memory reservation, and imposes no address constraints.

2. **Pure BSS Coalescing:**
   - When a segment has `file_size == 0` (zero-filled BSS) and matches the `virtual_address` of an already-loaded pure BSS segment:
     - The segment's byte buffer is expanded with zero-fill if `memory_size` exceeds the current segment size.
     - The active range extent `[start, end)` is updated to the maximum of the existing and new `end` addresses.
     - Additional allocated bytes are validated against `maximum_total_memory`.
     - Flags (`PF_R`, `PF_W`, `PF_X`) are combined via bitwise OR.
     - The duplicate header is not registered as an independent segment, preventing spurious range collisions.

3. **Strict Validation for Initialized Segments:**
   - Any segment with `file_size > 0` continues to undergo strict disjoint range validation against all other segments. This ensures that initialized data and executable code segments never overlap or overwrite one another.

## Consequences

- **100% Retail Recompilation:** All eight sampled retail executables (`Black`, `Bully`, `Downhill Domination`, `God of War II`, `GTA San Andreas v3.00`, `GTA San Andreas Indonesian v6`, `Need for Speed Most Wanted`, and `Resident Evil 4`) now recompile end-to-end to native C++20 and produce valid 32 MiB physical RAM images.
- **Safety:** Range checks remain strictly enforced for non-BSS segments. Total loaded memory across coalesced segments is rigorously checked against the configured memory limits.
- **Automated Verification:** Added unit test `coalesces_overlapping_pure_bss_segments_and_ignores_empty_segments` in `rust/frontend/src/elf.rs`.
