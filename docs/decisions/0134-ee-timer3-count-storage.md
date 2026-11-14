# ADR 0134: EE Timer 3 count register as 16-bit storage

- **Status:** Accepted.
- **Date:** 2026-11-14.
- **Scope:** EE Timer 3 MMIO modeling, engine run report, runtime ABI.

## Context

ADR 0129 classified the Downhill Domination USA probe stop (`0x800761A4`,
`lw $20, 0($2)`, `$2 = 0xFFFFFFFFB0001800`) as a read of `T3_COUNT` through the
KSEG1 alias of physical `0x10001800`, and ADR 0131 modelled `T3_COMP`
(`0x10001820`) as a 16-bit storage register while keeping `T3_COUNT` absent
from the MMIO dispatch table. PS2TEK documents the timer register surface as a
16-bit `TN_COUNT` whose guest writes set the counter, a 32-bit `TN_MODE`, and a
16-bit `TN_COMP`. The faulting path reads `T3_COUNT` with `timer3.mode = 0`
(CUE clear), i.e. the counter is disabled and was never written, so its
architectural value is the reset value.

## Evidence

- **Observed (fresh probe 2026-11-13, Windows x64/MSVC, runtime ABI 2):** stop
  `unsupported_memory_access @ 0x800761A4` after 3,890,274 instructions;
  `timer3.mode = 0`, `timer3.compare = 0`. See ADR 0131 validation.
- **Public register documentation:** PS2TEK EE Timers and PS2SDK `ee_regs.h`
  define `T3_COUNT` as the 16-bit counter at `0x10001800`; writes to `TN_COUNT`
  set the counter value.
- **Derived:** with `T3_MODE = 0` the counter is not counting (CUE clear), so a
  read returns the stored/reset value; modelling `T3_COUNT` as write-settable
  16-bit storage is architecturally grounded and does not fabricate counter
  progression.

References:
- PS2TEK, EE Timers: https://israpps.github.io/ps2tek/
- PS2SDK EE register definitions: https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/ee_regs.h

## Decision

1. Add `T3_COUNT` (`0x10001800`) to the MMIO dispatch table as a 16-bit
   storage register: reads return the stored value zero-extended, writes
   retain only bits 0..15 (guest writes set the counter), reset value 0.
2. Keep counter progression, compare matching, overflow events and interrupt
   delivery unmodelled. Counting in any clock mode (including the observed
   HBLANK mode `0x583`) remains blocked on a deterministic guest-time source;
   host wall-clock time is not an acceptable implicit guest-time source.
   A read while the counter is disabled returns the stored value, which is the
   defined reset/written state, not a fabricated advance.
3. Publish `timer3.count` in the RG REPS2 ENGINE run report next to
   `timer3.mode` and `timer3.compare`.
4. The `EeMemory` layout gains one field, so `REPS2_RUNTIME_ABI_VERSION` is
   incremented from 2 to 3; modules built against ABI 2 are rejected by the
   engine's runtime-ABI admission check.

## Consequences

- The Downhill probe can pass the `T3_COUNT` read at `0x800761A4` with the
  architecturally correct reset value while `T3_MODE = 0`; the next stop is
  whatever the title does with that value. This advances evidence collection;
  it is not a boot stage and not a playability claim.
- If a title enables counting (CUE set) and depends on the counter advancing,
  the stored value stays put and the run remains honest about the missing
  timing model; the failure mode is a wrong guest value, not a silent success
  of an unimplemented device event. This limitation stays disclosed.
- Generated native modules embedding the ABI-2 layout are rejected until
  rebuilt against runtime ABI 3.

## Validation

- C++ regression coverage in `reps2_hle_syscall_codegen_smoke`: `T3_COUNT`
  reset read 0, write/read 16-bit masking, and physical/KSEG0/KSEG1 alias
  agreement for reads and writes.
- The generated native build driver smoke asserts `timer3.count` exists in the
  run report and is 0 at reset, and asserts runtime ABI 3 in the toolchain
  report.
- Local verification (Windows x64): `cargo fmt --all -- --check`,
  `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked`, CMake Release build and
  `ctest -C Release` (41/41) all pass.
- No real-title probe is rerun in this change's validation note; the M4
  frontier classification and compatibility states are unchanged.
