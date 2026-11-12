# ADR 0101: EE semaphore signal/wait services and the honest WaitSema block

- **Status:** Accepted
- **Date:** 2026-11-09
- **Scope:** shared C++20 runtime (`ee_state.hpp`, `ee_hle.hpp`), EE syscall HLE surface, generated-probe observations
- **Related target:** Downhill Domination USA SCUS_971.77; observed guest PCs `0x0025A700` (`SignalSema` stub), `0x0025A710` (`iSignalSema` stub), `0x0025A724` (`WaitSema` stub), `0x0025B110` (`WaitSema` caller), `0x0025B1D0` (`CreateSema` caller)
- **Depends on:** ADR 0062 (EE syscall HLE dispatch), ADR 0066 (`CreateSema` and the 256-object table), ADR 0100 (IOP-to-EE SIF0 RPCINIT handshake)
- **Supersedes:** the ADR 0066 statement that `DeleteSema`/`SignalSema`/`WaitSema` are unimplemented, for the signal/wait/delete subset modelled here.

## Context

ADR 0100 released the EE RPC wait loop at `0x0025C928`; the title then called
`sceSifBindRpc`, sent the outbound `SIF_CMD_RPC_BIND`, and stopped as
`unhandled_syscall` on `WaitSema` (`0x44`) at `0x0025A724` with `$a0 = 3`. The
recorded M4 frontier was "who signals that semaphore".

`WaitSema` is a supported EE kernel service, not an unknown number. The gap was
that the HLE layer modelled only `CreateSema` (ADR 0066): there was no
signal/wait/delete behavior and no way to describe a genuine block. This ADR
settles the semaphore service semantics from evidence and models the subset the
observed path needs, without fabricating a completed wait.

## Evidence

- **Observed (pinned PS2SDK `d41ea62946e53d9a0c8788bb9eb8c8faf488b559`,
  `ee/kernel/include/kernel.h`):** the semaphore API is `CreateSema`,
  `DeleteSema`, `SignalSema`/`iSignalSema`, `WaitSema`, `PollSema`/`iPollSema`,
  `ReferSemaStatus`/`iReferSemaStatus`, `iDeleteSema`. All return `s32`. The
  public parameter type is `ee_sema_t { int count, max_count, init_count,
  wait_threads; u32 attr, option; }` — so only `init_count` and `max_count` are
  inputs, matching the ADR 0066 model.
- **Observed (target disassembly, `mips64r5900-el-ps2-objdump`):** the syscall
  stubs are contiguous at `0x0025A6E0` (`li v1,64; syscall`, `CreateSema`),
  `0x0025A6F0` (`li v1,65`, `DeleteSema`), `0x0025A700` (`li v1,66`,
  `SignalSema`), `0x0025A710` (`li v1,-67`, `iSignalSema`), `0x0025A720` (`li
  v1,68`, `WaitSema`) and `0x0025A730` (`li v1,69`, `PollSema`). The observed
  `pc=0x0025A724` is therefore the `syscall` word inside the `WaitSema` stub and
  the negated `iSignalSema` form confirms dispatch-by-magnitude is required.
- **Observed (target disassembly, semaphore creation):** the caller at
  `0x0025B0D0` waits with `jal 0x25a720` and a delay slot `lw a0,4608(s6)` where
  `s6 = 0x00760000`, i.e. the id is read from the global at `0x00761200`. That
  global is written at `0x0025B1E0` (`sw v0,4608(s1)`) with the `CreateSema`
  result from `0x0025B1D0`. The parameter block built at `0x0025B1C4`..`0x0025B1D0`
  is `sw zero,56(sp)` (`init_count = 0`) and `sw 255,52(sp)` (`max_count = 255`)
  with the block based at `sp+48`. Whether the semaphore stored at `0x00761200`
  is the one the probe blocks on is **not established**; it is recorded as a
  separate observed `CreateSema` caller.
- **Observed (real-title probe on this revision):** the probe blocks on
  `WaitSema` (`0x44`) at `0x0025A724` with `$a0 = 3` and reports
  `sema_wait=1`, `sema_op_id=3`, `sema_wait_count=0`, `sema_wait_blocked=1`,
  `sema_id=3`, `sema_init=0`, `sema_max=1`. The waited-on semaphore is the most
  recently created object (id 3), built with `init_count = 0`, `max_count = 1`,
  and its count is zero when the wait runs. This is the authoritative statement
  of the blocked object.
- **Observed (target disassembly, the caller ignores the result):** after
  `jal 0x25a720` the caller reloads the id and never tests `$v0`, so the guest
  does not depend on a specific `WaitSema` return value on this path.
- **Derived:** because the waited object's `init_count = 0` and no modelled
  component signals it before the wait, the guest genuinely blocks. The signal
  that would release it is the EE's own packet/queue bookkeeping driven by the
  IOP RPC bind reply, which REPS2 does not produce (ADR 0100 limitation).
- **Unknown:** the kernel's full numeric failure taxonomy for an invalid id and
  for a signal on a full semaphore (only the documented `-1` failure and the
  documented `0` success are used; a full semaphore is refused rather than
  reported as a successful no-op); the `PollSema`/`ReferSemaStatus` return
  values, which are therefore left unimplemented rather than guessed.

## Decision

1. Add a shared `find_ee_sema(EeHleState&, id)` resolver over the ADR 0066
   `CreateSema` table; an id outside the 256-object bound or a free slot resolves
   to "no live object".
2. Implement `DeleteSema` (`0x41`): release the slot, return `0`; an id with no
   live object returns the documented `-1` and changes nothing.
3. Implement `SignalSema` (`0x42`) and `iSignalSema` (`0x43`): raise the count
   when it is below the guest-supplied `max_count` and return `0`. A signal on a
   semaphore already at `max_count` cannot raise the count; because the pinned
   sources do not establish the kernel's overflow behaviour, the call is refused
   with the documented failure value and changes nothing rather than reporting
   success for a no-op. There is no waiter list and no scheduler, so no thread is
   made runnable and nothing else changes. Dispatch resolves the negated i-form by
   magnitude, so both numbers share one handler.
4. Implement `WaitSema` (`0x44`): consume a free count and return `0`; on a zero
   count, stop at the SYSCALL as the new `Status::semaphore_wait_blocked` instead
   of returning a fabricated success; an id with no live object returns the
   documented `-1` (a failure, not a block).
5. Report the new state in the generated probe (`sema_delete`, `sema_signal`,
   `sema_wait`, `sema_op_id`, `sema_op_count`, `sema_wait_count`, `sema_op_valid`,
   `sema_wait_blocked`).
6. Leave `PollSema`/`iPollSema`/`ReferSemaStatus`/`iReferSemaStatus` unimplemented
   so they keep stopping as `unhandled_syscall`; their non-blocking failure and
   status-write contracts are not established by the pinned sources.

## Validation

- Rust: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked`, including
  `probe_bakes_budget_and_emits_a_machine_readable_trace` (the trace-format test
  now also asserts the new semaphore fields).
- CMake Release build and `ctest -C Release`, including
  `reps2_hle_syscall_codegen_smoke` and `reps2_native_build_driver_smoke`.
- New runtime cases in `hle_syscall_smoke.cpp`:
  - `SignalSema` raises the count, refuses a signal on a semaphore already at
    `max_count` and refuses an invalid id with `-1`, leaving other objects
    unchanged;
  - `WaitSema` consumes a free count and writes the documented `0`;
  - `WaitSema` on a zero count stops as `semaphore_wait_blocked` with PC still at
    the SYSCALL, `$v0` unchanged, and no COP0 context created;
  - `WaitSema` on an invalid id (including a negative 32-bit id) is a failure
    (`-1`) that completes, not a block, and leaves no stale block flag;
  - `DeleteSema` frees the slot and a second delete is the documented failure;
  - a dispatch case exercising success, block, the negated `iSignalSema` number
    and delete through the generated CFG.
- Real-title probe: regenerated on this revision from the extracted boot ELF with
  `--discover --tu-count 8 --native-build --parent-iso "<owned ISO>"
  --probe-budget 5000000 --hle-syscalls` and both relocated regions, built with
  MSVC Release on the exact ISO (`0fe2b777...8a7eb86`) and boot ELF
  (`adfda7b7...f0cf0c`) digests. Observed trace after this ADR:
  `executed=3,169,504`, `status=16` (`semaphore_wait_blocked`), `pc=0x0025A724`,
  `hle_number=0x44` (`$a0 = 3`), `hle_name=WaitSema`, `syscalls=70`,
  `hle_handled=52`, `sema_calls=4`, `sema_live=4`, `sema_id=3`, `sema_init=0`,
  `sema_max=1`, `sema_wait=1`, `sema_op_id=3`, `sema_wait_count=0`,
  `sema_wait_blocked=1`, `sif_cmd_cid=0x80000009` (`SIF_CMD_RPC_BIND`). The title
  is now stopped with a precise, honest reason — a zero-count semaphore wait —
  instead of `unhandled_syscall` (`status=14`) on an unknown service (`0x44`). The
  instruction count and PC are unchanged from ADR 0100, which confirms that
  `WaitSema` was the last boundary and that no count was fabricated to pass it.

## Consequences

- `WaitSema` is now a real service with the documented count semantics, so the
  M4 frontier changes from "unknown service `0x44`" to a precise, honest stop:
  the guest is blocked on a zero-count semaphore that only the unmodelled IOP RPC
  bind reply would release. This is a fidelity improvement, not a boot stage.
- `SignalSema`, `iSignalSema` and `DeleteSema` are now available to the shared
  runtime, which the future IOP/IPC model needs to release such waits, without
  any fabricated hardware event.
- The new `Status::semaphore_wait_blocked` distinguishes "the guest chose to
  block" from "the host does not implement the service", preserving ADR 0062's
  rule that an unimplemented service stops as `unhandled_syscall`.

## Limitations and next evidence

- No scheduler and no waiter list exist, so a blocked thread cannot be resumed;
  `WaitSema` on a zero count is a terminal stop, not a resumable yield.
- Releasing the observed wait needs the M5 IOP subsystem: an IOP-side RPC
  server/binding model that replies to `SIF_CMD_RPC_BIND` with `SIF_CMD_RPC_END`,
  plus the SIF0/SIF1 DMA-completion and EE INTC interrupt chain that dispatches
  those packets. Publishing an IOP address, buffer, DMA completion, interrupt or
  RPC result without that device state would fabricate it.
- `PollSema`/`iPollSema`/`ReferSemaStatus`/`iReferSemaStatus` remain unimplemented.
- No guest-visible boot stage or Playable status is established by this ADR.
