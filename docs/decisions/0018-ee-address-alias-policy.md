# ADR 0018: EE Address Alias Policy and Main-RAM Segment Translation

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** Address translation, ELF executable byte coverage, CFG root admission, and jump region calculation for EE memory aliases

## Context

PlayStation 2 executables routinely describe their `PT_LOAD` program headers in the direct physical mapping (`0x00100000..`), while a significant portion of their control transfers run through cached (`KSEG0`: `0x80000000..=0x9FFFFFFF`) or uncached (`KSEG1`: `0xA0000000..=0xBFFFFFFF`) aliases of the same physical memory. A static recompilation toolchain must faithfully handle both views of the guest code without making unwarranted assumptions about unmaterialized regions or hardcoding title identities.

Furthermore, on the Emotion Engine R5900, `J` and `JAL` form target addresses by combining the lower 26 bits of the instruction word (shifted left by 2) with the **high 4 bits of the delay slot address (`PC + 4`)**, unlike classic MIPS32 which preserves only the upper 3 bits. This means jumps initiated from KSEG0 (`0x80000000`) target KSEG0 or KSEG1 depending on delay-slot page crossing, and cannot arbitrarily jump across region boundaries.

## Decision

1. **EE Address Layout Model (`address.rs`):**
   - Classify 32-bit guest addresses into EE segments: `Kuseg` (`0x00000000..=0x1FFFFFFF`), `PhysicalAlias` (`0x20000000..=0x3FFFFFFF`), `Kseg0` (`0x80000000..=0x9FFFFFFF`), and `Kseg1` (`0xA0000000..=0xBFFFFFFF`).
   - Define physical main RAM address translation for payloads under 32 MiB (`EE_MAIN_RAM_SIZE`). Addresses in scratchpad, IOP RAM, hardware registers, or unmapped space explicitly return `None`.
   - Provide `file_backed_executable_segment` to require that an instruction fetch in an alias region be backed by a materialized executable segment in that region.
   - Provide `executable_file_bytes` to read bytes across alias views for physical main RAM offsets covered by file bytes of an executable segment.

2. **CFG Root Admission and Budget:**
   - Entry points and roots are validated against executable file bytes (either directly in their declared segment or via an EE alias of main RAM).
   - Duplicate views of the same physical entry point are deduplicated.
   - Aliased root views are bounded by `AnalysisLimits::max_aliased_roots` (default 4, corresponding to the architectural maximum of EE RAM views). If the budget is exceeded, analysis deterministically returns `AnalysisError::AliasedViewLimitExceeded`.
   - CFG traversal within an admitted aliased view permits decoding of blocks and targets within that view.

3. **R5900 Jump Target Region Calculation:**
   - In `mips.rs`, `jump_target` carries bits 31..28 of `PC + 4` (including carry out of bit 31 into host representation before 32-bit guest masking) into the target address.

## Validation

- Address classification and translation tests confirm correct mapping of Kuseg, PhysicalAlias, Kseg0, and Kseg1, and rejection of non-RAM spaces.
- Unit tests verify that alias duplicates do not inflate CFG block budgets, that alias view limits are enforced, and that calls through `JALR` within KSEG0 resolve correctly without guessing unmaterialized code.
- MSVC and CMake test suites pass 100% with no warnings.

## References

- Sony Computer Entertainment, *EE Core Instruction Set Manual*, Version 6.0 (2002).
- MIPS Technologies, *MIPS32 Architecture for Programmers, Volume II: The MIPS32 Instruction Set*.
