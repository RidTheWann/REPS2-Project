# ADR 0076: EE StartThread, GetThreadId and ChangeThreadPriority with a ready-queue model

- Status: Accepted
- Date: 2026-11-06
- Related: ADR 0075 (CreateThread dormant records), ADR 0062 and 0064 (EE
  syscall HLE), ADR 0071 (bounded syscall sequence)

## Context

After ADR 0075 the Downhill Domination probe services `CreateThread` (0x20) and
stops at `StartThread` (0x22) at `pc=0x0025A504` with `$a0=0x00000002` (the id
REPS2 returned) and `$a1=0x00761208` (the argv block). The caller is the game's
`InitThread` at `0x0025B1A8`; its disassembled chain is:

- `0x0025A6E0` wrapper (service 0x40) creates a semaphore with the block at
  `$sp+0x30` (`init_count=0`, `max_count=0xFF`), storing the id at `0x00761200`.
- The nine-word thread block at `$sp` is filled with `func=0x0025B0D0`,
  `stack=0x00760E00`, `stack_size=0x400`, `gp=0x002A40F0`,
  `initial_priority=0`. `status`, `current_priority`, `attr` and `option` are
  left uninitialized by the caller.
- `CreateThread($sp)` through the `0x0025A4E0` wrapper (service 0x20). A
  negative `$v0` jumps over the rest of the sequence.
- `StartThread(id, 0x00761208)` through the `0x0025A500` wrapper (service 0x22).
- `GetThreadId()` through the `0x0025A5D0` wrapper (service 0x2F), then
  `ChangeThreadPriority($v0, 1)` through the `0x0025A570` wrapper (service 0x29).

Public behavioural references (retrieved 2026-11-06):

- ps2sdk `ee/kernel/include/kernel.h`: `s32 StartThread(s32 thread_id, void
  *args)`, `s32 GetThreadId(void)`, `s32 ChangeThreadPriority(s32 thread_id,
  s32 priority)`; `MAX_THREADS 256` and thread 0 is the idle thread; status
  values `THS_RUN 0x01`, `THS_READY 0x02`, `THS_WAIT 0x04`,
  `THS_SUSPEND 0x08`, `THS_WAITSUSPEND 0x0C`, `THS_DORMANT 0x10`.
- ps2sdk `ee/kernel/src/thread.c` calls `StartThread(topId, &topArg)` without
  checking the result, then `ChangeThreadPriority(GetThreadId(), 1)`, and guards
  the value with `priority < 128`.
- ps2tek records that boot "creates a thread with priority 0 (main thread)".
- Open PS2 Loader patch notes record titles that hardcode the main thread id, so
  ids are kept explicit rather than derived from pointers.

## Decision

1. `CreateThread` marks the record `THS_DORMANT` and sets its current priority
   to the observed initial priority. Created ids remain `2..17`; id 1 is the
   boot thread (ADR 0075).
2. `StartThread(id, args)` accepts only an existing DORMANT record from that id
   range. It stores `args` opaquely (the kernel does not dereference it until the
   thread runs), sets `THS_READY`, inserts the id into a bounded ready queue
   ordered by ascending current priority with FIFO order among equals, and
   returns 0. Unknown ids and non-dormant records return `-1` and change nothing.
3. `GetThreadId()` returns `kEeMainThreadId` (1) because REPS2 executes exactly
   one translated context. The value is honest for the boot thread and must be
   replaced when a dispatcher can run a second context.
4. `ChangeThreadPriority(id, priority)` accepts 0..127 (the ps2sdk usage range).
   Id 1 updates the recorded main-thread priority; a created id must exist. A
   READY thread is removed and re-inserted so the queue stays ordered. Refusals
   return `-1` and change nothing.
5. No service switches context. The boot thread keeps executing after
   `StartThread`; a real dispatcher is a separate, not-yet-implemented contract.

## Consequences

- The observed `InitThread` chain executes without fabricated thread execution.
  The probe advances to the next unimplemented service, preserving the exact
  boundary.
- The ready queue, status values, argv pointer and main-thread priority are host
  state. No guest memory is written, and the guest's uninitialized words are
  never interpreted.
- `ReferThreadStatus`, `SleepThread`, `WakeupThread`, `ExitThread`,
  `DeleteThread`, dispatch and all blocking semantics remain unimplemented.
- A title that depends on immediate dispatch at `StartThread` will not progress
  past the next blocking service. That is reported, not hidden.

## Verification

- `reps2_hle_syscall_codegen_smoke` grew from 39 to 47 cases: DORMANT→READY
  transition, argv storage, priority ordering, equal-priority FIFO, unknown id
  and non-dormant refusal, boot-thread id, main and created priority changes,
  queue reordering, invalid priority/id refusal, and dispatcher routing for
  0x22, 0x29 and 0x2F.
- Real-title probe (2026-11-06): `status=14`, `pc=0x0025A794`,
  `executed=2,946,399`, `syscalls=32`, `hle_handled=21`. The sequence records
  `0x0025A504:34:H:0x00000002:0x00761208:...` (StartThread),
  `0x0025A5D4:47:H:...:0x00000000` (GetThreadId) and
  `0x0025A574:41:H:0x00000001:0x00000001:...` (ChangeThreadPriority with the
  boot id returned by GetThreadId), then stops at
  `0x0025A794:75:U:0x0093C7C0:...` (`GetOsdConfigParam`). The next boundary is
  exactly one service beyond the previous one.
- Probe details are recorded in `docs/PROJECT_STATE.md`, `docs/RESEARCH.md` and
  `docs/COMPATIBILITY_MATRIX.md`.
