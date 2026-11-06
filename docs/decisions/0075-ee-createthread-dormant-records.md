# ADR 0075: EE CreateThread dormant-thread records

- **Status:** Accepted
- **Date:** 2026-11-06
- **Scope:** opt-in EE syscall HLE layer, M4 real-title execution

## Context

After the `InitAlarm` patch (ADR 0073) the Downhill Domination probe reaches
service `0x20` at `PC=0x0025A4E4` with `$a0=0x0093C7B0`. The caller at
`0x0025B1A8` is the game's `InitThread`:

- it first calls `CreateSema` for `topSema` (observed as the third CreateSema),
- it builds a 36-byte `ee_thread_t` on its own stack at `$sp`:
  `+0x04` func `0x0025B0D0`, `+0x08` stack `0x00760E00`, `+0x0C` size `0x400`,
  `+0x10` gp `0x002A40F0`, `+0x14` priority `0`,
- it leaves the `status` word at `+0x00` uninitialized,
- it calls `CreateThread`, stores the returned id at `0x00282150`, and then
  calls `StartThread` (0x22) with an args block at `0x00761208`.

`CreateThread` therefore must return a usable id without interpreting the
guest's uninitialized status word. It cannot schedule anything: no scheduler or
thread context switching exists in the runtime.

## Evidence

- ps2sdk `kernel.h` declares `s32 CreateThread(ee_thread_t *thread)` and defines
  `ee_thread_t` as nine 32-bit words: `status`, `func`, `stack`, `stack_size`,
  `gp_reg`, `initial_priority`, `current_priority`, `attr`, `option`.
- The same header documents `MAX_THREADS 256` and states that thread 0 is the
  idle thread.
- ps2sdk `kernel.S` wraps service 0x20 as an ordinary `syscall`.
- ps2sdk `alarm.c`/`thread.c` usage shows `status` is not set by callers; the
  kernel owns it. The retail caller matches that pattern.
- The observed call's func, stack and gp all resolve in EE main RAM, the stack
  range `0x00760E00 + 0x400` stays inside it, and the priority is below the
  documented `MAX_PRIORITY 128`.
- ps2sdk declares the result `s32` and callers test `< 0` for failure; no
  specific negative code is documented.

## Decision

1. Read the nine-word parameter block through the normal EE memory path. An
   unreadable block, a zero/misaligned/unmapped `func`, a zero size, an
   out-of-main-RAM or overflowing `stack` range, a nonzero `gp` that does not
   resolve, or a priority outside `0..127` is refused with `-1`, and nothing is
   allocated. The reason is recorded for diagnostics.
2. Allocate the lowest free record in a bounded host table and return
   `index + 1`, so ids start at 1 and never collide with the documented idle
   thread 0. A full table is refused with `-1` rather than fabricating an id.
3. Record `func`, `stack`, `stack_size`, `gp`, initial priority, `attr` and
   `option`. Do not write guest memory: the parameter block is input only, and
   the guest's `status` word is never read or rewritten.
4. Do not create a status, stack image, argv block, ready-queue entry or
   scheduler state. `StartThread` (0x22), `ExitThread`, `ReferThreadStatus` and
   priority changes remain unimplemented and stop honestly when observed.

## Consequences

- The observed `InitThread` can obtain an id and proceed. The next stop is
  `StartThread` (0x22), confirmed by the probe, and it is not yet implemented.
- The host record table holds 16 threads, fewer than the documented kernel
  limit of 256. This is a host bound, not a kernel claim; exceeding it produces
  the documented negative failure and an explicit reason. A later increment may
  move thread records into reserved guest RAM or expand them once an observed
  title needs more.
- No thread actually runs until a scheduling decision is implemented. Passing
  `CreateThread` is not evidence of multithreading or a boot stage.

## Verification

- `reps2_hle_syscall_codegen_smoke` grew to 39 cases: a valid call returns id 1
  and records the fields while leaving the uninitialized status word untouched;
  each invalid parameter class is refused with `-1` and allocates nothing; the
  table bound returns ids 1..16 and then `table_full`; the generated dispatcher
  routes service 0x20 to the handler.
- Real-title probe (2026-11-06, boot ELF SHA-256
  `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`):
  `status=14` (`unhandled_syscall`), `executed=2,946,363`, `syscalls=29`,
  `hle_handled=18`. `CreateThread` is handled at `pc=0x0025A4E4` and the guest
  continues to `StartThread` (0x22) at `pc=0x0025A504` with `$a0=0x00000001`
  (the returned id) and `$a1=0x00761208` (the argv block). The next stop is
  exactly one service beyond the previous boundary.
- Real-title evidence is recorded in `docs/PROJECT_STATE.md` and
  `docs/RESEARCH.md`.
