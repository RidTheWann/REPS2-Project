# ADR 0047: EE instruction-fetch address error on misaligned PCs

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** EE runtime address-error classification, generated CFG dispatcher, native control-flow regression coverage, and capability/roadmap documentation

## Context

The runtime already delivers Level-1 AdEL/AdES for checked data accesses that violate natural alignment. The generated CFG dispatcher, however, treated every PC absent from its translated block table as `Status::external_control_flow`. That conflated two materially different cases:

- an aligned PC that is outside the translated CFG, which is a normal host/translation boundary; and
- a PC that cannot be fetched as an EE instruction because it is not aligned to a 32-bit instruction boundary, which is an architectural Address Error on Load or instruction fetch (AdEL).

The distinction applies not only to an externally supplied start PC. It also applies after a register-indirect jump/return and after ERET restores an EPC/ErrorEPC target. Without a check at the shared dispatcher boundary, all of those paths could hide an architectural exception as an ordinary CFG miss.

## Architectural evidence

Sony's *Emotion Engine Core User's Manual*, version 6.0, section 4.2.9 (Address Error Exception), defines this as a Level-1 exception routed through the common exception vector. It assigns ExcCode 4 to load and instruction-fetch address errors and identifies a word-misaligned instruction fetch as a cause. The exception state records the faulting/restart PC in EPC, sets or clears Cause.BD according to whether the fault occurred in a branch delay slot, and records the bad virtual address in BadVAddr.

Primary reference:
- Sony, *Emotion Engine Core User's Manual*, version 6.0, section 4.2.9: https://docs.alexrp.com/mips/ee.pdf

The existing `raise_ee_address_error` helper already implements the shared Level-1 frame behavior, including nested-EXL preservation and BEV-selected V_COMMON. This ADR reuses that path rather than implementing a second exception-entry mechanism.

## Decision

### 1. Give instruction fetch an explicit AdEL reason

Add `AddressErrorCode::InstructionFetch = 4`. It intentionally shares the architectural ExcCode value with `AddressErrorCode::Load`; AdEL is the common exception class for both. `Store` remains ExcCode 5 (AdES).

### 2. Validate PC alignment before CFG lookup

At the top of every `execute_cfg` dispatch iteration, before binary-searching the block table, test whether `state.pc` is word-aligned:

```cpp
if ((state.pc & 0x3U) != 0U) {
    return raise_ee_address_error(
        state, AddressErrorCode::InstructionFetch, state.pc, false);
}
```

Because the check is in the shared dispatcher, it covers the initial PC, post-delay-slot branch/jump targets, JR/JALR targets, and EPC/ErrorEPC targets restored by ERET. It also applies identically to single-translation-unit and split-translation-unit output.

### 3. Preserve precise exception state

An instruction-fetch alignment fault occurs at the next fetch boundary, after a preceding branch's delay slot has completed. Therefore the fetch fault is not itself a delay-slot exception: BD is clear and EPC identifies the misaligned fetch PC when EXL was clear. BadVAddr records the same misaligned virtual address. The existing helper preserves an earlier EPC/BD frame if EXL is already set, and selects V_COMMON using BEV.

### 4. Keep unrelated fetch failures distinct

This change does not classify an aligned PC missing from the translated block table as AdEL. Such a PC remains `external_control_flow`. TLB misses/refills, execute-permission faults, translation errors, bus errors, and instruction-fetch device behavior remain outside the implemented model. No BIOS/HLE service, TLB model, or title-compatibility claim is introduced.

The build-manifest schema is unchanged.

## Validation

The change adds:

- Rust code-generation assertions that the dispatcher emits the instruction-fetch alignment guard and routes it through `raise_ee_address_error`.
- Native control-flow smoke cases for an unaligned externally supplied PC, unaligned JR/JALR targets after their delay slots, a misaligned ERET target, and the existing ERET-to-JR path.
- Reuse of the same `tests/control_flow_smoke.cpp` source for single-unit and split-unit generated CFG tests.

Verified by pull-request CI run 41443941700: Ubuntu/GCC and Windows/MSVC both pass the CMake build and all 16 CTest cases, including single-unit and split-unit control-flow smoke tests; Windows CLI smoke passes. Rustfmt, Clippy, Rust unit tests, and the Rust 1.88 MSRV job also pass.

## Consequences

- A misaligned guest instruction-fetch PC now produces a guest-visible Level-1 AdEL transition instead of being mistaken for a host CFG boundary.
- The exception implementation remains centralized in `raise_ee_address_error` / `raise_ee_exception`.
- Aligned external control flow remains an explicit boundary, and the broader EE virtual-memory, TLB, bus, permission, and interrupt models remain open.
