# ADR 0069: EE guest syscall handlers invoked through SetSyscall

- **Status:** Accepted
- **Date:** 2026-11-05
- **Scope:** M4 real-title boundary, EE syscall HLE layer

## Context

REPS2 already host-services a small set of EE kernel syscalls (SetupThread,
SetupHeap, CreateSema, FlushCache) through `dispatch_ee_syscall`, and titles can
replace kernel slots with `SetSyscall` (service 0x74). ps2sdk itself installs a
kernel-side printf handler for service 0x75 this way, and its `_print` wrapper
reaches it through an ordinary `SYSCALL` trap. Downhill Domination calls
`SetSyscall` in its boot code, so a registered guest handler can be reached by
guest code at any time.

The kernel's real exception path saves the user register context before entering
the handler and restores it on return, keeping only `$v0` as the service
result. REPS2 has no privilege rings, so there is no kernel mode to perform
that save/restore.

## Decision

Host-serviced slots still take the fast path. A slot that a title has replaced
through `SetSyscall` is entered as translated guest code:

- The full guest context (GPRs, HI/LO, HI1/LO1, FPRs, ACC, FCSR) is saved into
  one frame of a bounded host stack (`kEeMaxSyscallHandlerDepth = 8`).
- `$ra` is replaced with the kseg3 sentinel `kEeSyscallHandlerReturnPc`
  (`0xFFFF_FFE0`), which is outside every EE mapping, word-aligned, and never
  fetched. The dispatcher recognizes it before any address check and completes
  the context restore.
- The handler's `$v0` is kept as the service result; everything else is
  restored from the frame. Execution continues at the saved resume PC.
- Nested syscalls from within a handler push another frame; depth overflow
  stops at `Status::syscall_handler_overflow` instead of recursing.

The handler itself must already exist in the translated CFG (reachable through
static roots or the new data-pointer candidate roots); otherwise execution
stops at `external_control_flow` and reports the missing block precisely.

## Alternatives considered

- **Host-stub every registered slot.** Rejected: invents behavior REPS2 cannot
  know (the handler is title code) and can deadlock titles that depend on the
  routine's real side effects.
- **Enter the handler without saving context.** Rejected: clobbers caller
  registers and breaks any handler that expects the syscall entry convention.
- **Model a full kernel exception entry (EPC/BD/Status.EXL).** Rejected:
  `SetSyscall` handlers are entered through an ordinary trap whose architectural
  state the title already manages through its own exception path; REPS2's
  architectural SYSCALL exception path is only taken when the HLE layer is
  disabled.

## Consequences

- Registered slots take precedence over host services of the same number, so a
  title can fully replace e.g. `FindAddress` (0x83) before calling it.
- Handler entry/exit is recorded (`guest_handler_dispatches`,
  `guest_handler_returns`, `last_syscall_resume_pc`) so probes can distinguish
  host-serviced calls from guest-handler calls.
- Depth overflow is a controlled stop, not silent misbehavior.
- No fake success: an unknown or unregistered service still stops at
  `unhandled_syscall`.

## References

- ps2sdk `ee/kernel/src/setup_syscalls.S` (`setup` = `li $v1, 0x74; syscall`).
- ps2sdk `ee/kernel/src/libosd.c`: `_print` resolves `printf` through
  `GetSyscallHandler(__NR_ResetEE)` then calls it through an ordinary SYSCALL.
- ADR 0068: `FindAddress` (0x83) remains deferred until its ABI is evidenced.
