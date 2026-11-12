# ADR 0131: EE Timer 3 compare register and run-report timer state

- **Status:** Accepted.
- **Date:** 2026-11-13.
- **Scope:** EE Timer 3 MMIO modeling and engine run-report diagnostics.

## Context

ADR 0129 corrected the Downhill Domination frontier: execution stops at a `T3_COUNT` read through the KSEG1 alias `0xB0001800` (`0x800761A4`, `lw $20, 0($2)`). Before implementing counter progression, the ADR requires the title's Timer 3 setup, writes, compare values and consumer behavior to be captured.

Static inspection of the generated translation units for the owner-supplied Downhill Domination USA ISO (ISO SHA-256 `0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`) shows the relocated kernel patch configuring Timer 3:

- **Observed:** `0x800763EC`..`0x800763F4` builds `0xB0001810` and conditionally stores mode value `0x83` (clock select 3 = HBLANK, `CUE` set).
- **Observed:** `0x80076460`..`0x80076480` stores a runtime value from `$a0` to `0xB0001820` (`T3_COMP`) and then writes mode `0x583` (HBLANK clock, `CUE`, `CMPE`, and the write-one-to-clear compare flag).
- **Observed:** `0x800761A4` reads `0xB0001800` (`T3_COUNT`) after a setup path that also stores caller state on the stack, and `0x80076408` reads the same register in a `jal` delay slot.
- **Derived:** both paths require a documented Timer 3 counter model with a deterministic guest-time source before execution can continue.

PS2TEK documents Timer 3 as reserved by the BIOS for alarm functions and defines the register surface as a 16-bit `TN_COUNT` (writes set the counter), a 32-bit `TN_MODE` (clock select 0..3, gate fields, `ZRET`, `CUE`, `CMPE`, `OVFE`, and write-one-to-clear `EQUF`/`OVFF` flags), and a 16-bit `TN_COMP` compare value. The selected clock mode is HBLANK, whose cadence depends on a video timing model REPS2 does not yet have.

References:
- PS2TEK EE Timers: https://israpps.github.io/ps2tek/
- PS2SDK EE timer definitions: https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/timer.h

## Decision

1. Add `T3_COMP` (`0x10001820`) to the MMIO dispatch table as a 16-bit storage register: reads return the low 16 bits zero-extended, writes retain only bits 0..15, and reset value is zero.
2. Keep `T3_COUNT` (`0x10001800`) absent from the dispatch table. No counter value, host clock, synthetic progression, compare match, overflow event or interrupt is produced.
3. Publish the current `timer3.mode` and `timer3.compare` register state in the RG REPS2 ENGINE run report so real-title probes can distinguish "setup executed" from "counter read happened first".
4. Do not derive guest time from host wall-clock time or unstated instruction-cycle assumptions. A future counter model must define a deterministic guest-time source and test progression, compare, wrap, mode changes and interrupt routing.

## Consequences

- The shared `EeState`/`EeMemory` layout gains one field, so the runtime ABI is incremented from 1 to 2. Previously built native modules that embed the old layout are rejected by the engine's runtime-ABI admission check.
- A title can write and read back its Timer 3 compare value through physical, KSEG0 and KSEG1 addresses.
- The real-title stop at `T3_COUNT` remains explicit; this ADR advances evidence collection, not compatibility or boot state.
- The run-report schema gains a `timer3` object. Existing fields and stop statuses are unchanged.
- Timer 3 counting in HBLANK mode remains blocked on a video/bus timing model; no approximate cadence is introduced.

## Validation

- C++ regression coverage reads/writes `T3_COMP` through physical and alias addresses, verifies 16-bit masking, verifies reset value, and confirms `T3_COUNT` still fails closed without mutating the destination.
- The generated native build driver asserts the `timer3` report object and its zero reset values.
- Local verification: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked`, CMake Release build and `ctest -C Release` (40/40) pass.
- Real-title probe (Downhill Domination USA, exact ISO SHA-256 above, Windows x64/MSVC): stop `unsupported_memory_access @ 0x800761A4` after 3,890,274 instructions, `$2 = 0xFFFFFFFFB0001800`, `timer3.mode=0`, `timer3.compare=0`, 16-PC trace ending at `0x800761A4`. This confirms the Count read precedes every Timer 3 setup write on the faulting path.
