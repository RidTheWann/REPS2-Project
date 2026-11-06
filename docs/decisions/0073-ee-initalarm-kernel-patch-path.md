# ADR 0073: EE InitAlarm kernel-patch path (`T3_MODE` guard and 0x5B)

- **Status:** Accepted
- **Date:** 2026-11-06
- **Scope:** opt-in EE syscall HLE layer, M4 real-title execution

## Context

With the guest-visible syscall table from ADR 0072 in place, the Downhill
Domination init routine at `0x0025EF90` completes and execution reaches
`0x0025F330`. That routine is byte-for-byte the ps2sdk `InitAlarm` shape
(`ee/kernel/src/alarm.c`):

1. `if (((*T3_MODE) & 0x100) == 0)` — ps2sdk's comment: "Only apply the kernel
   patch if T3 has not already started". `T3_MODE` is `0x10001810`
   (`ee/kernel/include/timer.h`); ps2tek documents bit 8 as "compare interrupt
   enable".
2. `setup(0x5A, &kCopy)` then two `Copy` calls (blob to `0x80076000`, 0x740
   bytes; `eenull` to `0x00082000`, 0x28 bytes), two `FlushCache` calls.
3. `setup(0x5B, 0x80076000)`.
4. For slots `0xFC`, `0xFE`, `0xFD`, `0xFF`, `0x12C` (`Intc12Handler`) and
   `0x08`, `setup(slot, GetEntryAddress(slot))`.

The first access, a word load from `0x10001810`, stopped the probe as
`unsupported_memory_access` at `pc=0x0025F34C`. Slot `0x12C` also exceeds the
256-slot bound introduced with ADR 0067.

## Evidence

- ps2sdk `alarm.c` matches the observed constants, offsets and call order; the
  descriptors at `0x00283160` are `(0x5A, 0x0025F2E8)`, `(0x5B, 0x80076000)`,
  then the six slots above.
- ps2sdk `timer.h`: `T3_MODE ((volatile unsigned int *)0x10001810)`,
  `T3_COUNT 0x10001800`, `T3_COMP 0x10001820`; `Tn_MODE` is a 32-bit mode word.
- ps2tek documents Timer 3 mode bit 7 as timer enable and bit 8 as compare
  interrupt enable. No clock source is enabled out of reset, so the mode word
  reads 0 before the guest configures it — and the trace shows no earlier access
  to this register.
- ps2sdk `kernel.h` declares `void *GetEntryAddress(int syscall)` under "Helper
  functions for kernel patching"; `setup_syscalls.S` wraps it as service `0x5B`.
  Its `libosd.c` patch table pins the arithmetic:
  `0x80011F80 + (-15358 * 4) = 0x80002F88` is documented as an address relative
  to the start of the syscall table in 4-byte units. `GetEntryAddress` therefore
  returns the slot address `base + number * 4`, not the handler stored there.
- The observed `InitAlarm` loop uses `0x12C`, so the 256-slot registration bound
  is demonstrably too small.

## Decision

1. Model only the Timer 3 mode register: reset 0; writes retain the documented
   writable mode bits (`CLKS` 0-1, `GATE` 2, `GATS` 3, `GATM` 4, `ZRET` 6,
   `CUE` 7, `CMPE` 8, `OVFE` 9 — mask `0x3DF`); `EQUF`/`OVFF` read as zero.
   No counting, clock source, compare match, overflow or interrupt is modelled.
   All other EE timer registers remain unsupported stops.
2. Implement `GetEntryAddress` (0x5B) as `kEeSyscallTableBase + number * 4` with
   32-bit modular arithmetic and no range check; the result is sign-extended
   into `$v0` like any word result.
3. Raise the guest-visible table to 512 slots so the documented `0x12C` slot is
   accepted. Host registrations remain indexed for 0..255 and use a small sparse
   list for extended numbers so `EeState` stays compact. `SetSyscall` continues
   to require a nonzero, aligned handler in EE main RAM and now also requires the
   computed slot address to be writable.

## Consequences

- The ps2sdk `InitAlarm` path can run end to end: the guard reads 0 (patch
  applies), `Copy`/`FlushCache` use already-implemented services or registered
  guest handlers, and the six `GetEntryAddress`/`SetSyscall` pairs write real
  slot values.
- A guest that enables Timer 3 and sets the compare-interrupt bit will take the
  "already started" branch, exactly as the source describes, even though REPS2
  does not generate the interrupt.
- Timer-dependent code that polls `T3_COUNT`/`T3_COMP` or depends on timer
  interrupts remains unsupported and stops honestly.
- Negative/very large service numbers passed to `SetSyscall` are still refused;
  `GetEntryAddress` computes their slot address but registering them is not
  modelled until an observed title needs it.

## Verification

- `reps2_hle_syscall_codegen_smoke` grew to 35 cases: `GetEntryAddress` returns
  `0x80012430` for 0x12C and `0x80002F88` for the documented `0xFFFFC402`
  modular case; `SetSyscall` accepts 0x12C and writes both the table word and
  the host registration; `T3_MODE` resets to 0, retains only writable bits and
  clears the status bits.
- Real-title evidence is recorded in `docs/PROJECT_STATE.md` and
  `docs/RESEARCH.md`.
