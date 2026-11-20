# ADR 0173: Cooperative EE scheduler (runtime ABI 22)

- **Status:** Accepted (validated locally; see Validation).
- **Date:** 2026-11-20.
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp` (`EeThread` saved
  context and `waiting_on_sema`, `EeHleState` scheduler fields
  `active_thread_id`, `blocked_sema_id`, `main_context`, `main_context_live`,
  `main_ready_pending`, `scheduler_*` counters and
  `scheduler_switched_in_service`), `runtime/include/reps2/runtime/ee_hle.hpp`
  (`ee_scheduler_*` helpers, `hle_wait_sema` block path, `hle_signal_sema`
  wake path, `dispatch_ee_syscall` resume guard),
  `rust/frontend/src/control_codegen.rs` (dispatcher scheduling point),
  `engine/src/main.cpp` (`session.threads` report section), and
  `tests/hle_syscall_smoke.cpp` (four scheduler cases).
- **Related:** ADR 0164 (layout contract), ADR 0171 (ABI 21), ADR 0119
  (semaphore services), ADR 0153/0172 (interrupt delivery patterns).

## Context

The Downhill Domination ABI-21 frontier stops at `pc=0x00201888` with
`budget_limit_reached` after 79,999,980 instructions and 6,854,902
`FlushCache` calls from one hot site. The census and the new thread report
show the actual cause:

- Thread id 2 is created and started READY (`CreateThread` 1, `StartThread` 1,
  `ready_queue == [2]`, status READY) with `func = 0x0025b0d0`.
- That function is an event loop calling `WaitSema` (0x44), `iSignalSema`
  (0x43) and `CreateSema` (0x40) — a second guest context.
- REPS2 executed only the boot thread, so the READY thread never ran and the
  flag at `0x004CD400` that the boot thread waits on was never cleared.

The recorded `WaitSema` calls all had a free count, so the existing
`semaphore_wait_blocked` path never even triggered. Without a dispatcher, a
created READY thread could never be run by any code path.

## Decision

Implement a minimal, fail-closed cooperative scheduler.

1. **Context capture.** `EeThread` gains a saved execution image (pc, GPRs,
   HI/LO, HI1/LO1, FPRs, ACC, FCSR, SA, VU0, COP0) and `context_live`. The
   boot thread's image lives in `EeHleState::main_context`, since the boot
   thread has no record in `threads`.
2. **Blocking.** `WaitSema` on a zero count captures the caller's context,
   parks it on that semaphore id, and switches to the lowest-id READY thread.
   With nothing READY it returns `semaphore_wait_blocked`; the context stays
   parked, never destroyed.
3. **Waking.** `SignalSema`/`iSignalSema` on that id makes the waiter READY.
   A parked boot thread is resumed at the next dispatcher scheduling point
   (`main_ready_pending`), so a running created thread's context is never
   clobbered by the signal itself.
4. **First dispatch.** A thread that has never run starts at `function` with a
   fresh register file, `$sp = stack + stack_size`, `$gp` from the record, `$a0`
   from the start argument and `$ra` at the dispatcher sentinel. No register
   image is fabricated for a thread that has not run.
5. **Dispatcher point.** `execute_cfg` gains one scheduling point before each
   fetch: when no context is active and a thread is READY, dispatch it; when
   the boot thread is pending resume, resume it.
6. **Resume guard.** `dispatch_ee_syscall` must not overwrite `state.pc` with
   `resume_pc` when the service switched contexts; a one-shot
   `scheduler_switched_in_service` flag carries that.

A thread is never made READY by any event the runtime did not produce: only a
`SignalSema` on its blocked semaphore wakes it.

## Consequences

- `EeState` layout changes: `EeThread` grows by ~1.4 KB with its context image
  and the scheduler fields add ~1.5 KB of state; `REPS2_RUNTIME_ABI_VERSION`
  moves 21 -> 22, so every generated module must be rebuilt.
- A title that never blocks still behaves exactly as before: the scheduling
  point is a no-op when `ready_count == 0` and no context is parked.
- Only voluntarily blocked threads are suspended; no preemption, no
  timeslicing, no priority dispatch beyond lowest-id READY selection, and no
  wake path other than a signalled semaphore. These are explicit limits, not
  omissions: they are what the evidence supports.
- `GetThreadId` still reports `kEeMainThreadId` and `ReferThreadStatus` still
  reports the boot thread for `TH_SELF`; the scheduler does not yet switch the
  reported self id, which is tracked as follow-up work.

## Validation

- `tests/hle_syscall_smoke.cpp`: four new cases
  (`scheduler_dispatches_ready_thread_on_wait_block`,
  `scheduler_wakes_parked_boot_thread_on_signal`,
  `scheduler_preserves_suspended_boot_context`,
  `scheduler_reports_blocked_when_nothing_runnable`) plus the existing
  suite (198 cases).
- `cmake --build build --config Release --parallel` (MSVC 14.51, Windows x64)
  and `ctest -C Release`: 49/49.
- Rust gates: `cargo fmt --check`, `cargo clippy -D warnings`,
  `cargo test --workspace --locked` all pass.
- No real-title probe result is claimed by this ADR; the frontier is unchanged
  unless the second thread's dispatch actually clears the wait flag.

## Real-title result (ABI 21 module, pre-ABI-22 probe)

A probe of Downhill Domination USA (ISO SHA-256
`0fe2b777…a7eb86`, boot ELF `adfda7b7…f0cf0c`) with the scheduler built but the
module still at ABI 21 gives an **unchanged** frontier: `budget_limit_reached`
at `pc=0x00201888`, 79,999,980 instructions, the same 6,854,902 `FlushCache`
calls, thread 2 still READY and never dispatched.

- **Derived:** the scheduler did not fire because every one of the 134
  `WaitSema` calls in this run received a free count (`refused = 0`,
  `semaphore_wait_blocked` never raised). The boot thread never voluntarily
  blocks, so the second thread is never selected. The scheduler is correct but
  not yet *reachable* on this title's path.
- **Unknown:** what makes the boot thread block in the real kernel at this
  point. The evidence says the wait flag `0x004CD400` is produced by a path the
  boot thread does not reach, not by the READY thread being starved of the CPU.
- **Not claimed:** any frontier advance. This ADR is recorded as a scheduler
  capability, with the reachability gap left explicit.

## Real-title result (ABI 22 module, rebuilt)

The probe above used an ABI-21 module. A fresh probe against a rebuilt ABI-22
module is required before any claim about the scheduler's effect on the
frontier, and is left as the next probe task. No result is asserted here.