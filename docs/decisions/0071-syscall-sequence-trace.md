# ADR 0071: Bounded EE syscall-boundary sequence trace

- **Status:** Accepted
- **Date:** 2026-11-06
- **Scope:** M4 real-title diagnosis, native probe instrumentation

## Context

The EE HLE layer already records the *last* syscall boundary:
`last_syscall_pc`, `last_syscall_number`, `last_syscall_arguments`,
`last_syscall_v0_before`, plus the post-dispatch result and resume PC. That was
enough while every boundary was a host service.

ADR 0069 added registered guest handlers, which are entered as translated guest
code and can themselves issue further syscalls. A trace that keeps only the last
boundary cannot say which of several call sites produced it, nor in what order
the boundaries occurred. The Downhill Domination probe exposed exactly that gap:
its final `FindAddress` boundary reports `$a0=0x00000004`, while the routine has
four static call sites, two of which load `0x80000000` into `$a0` and two of
which use `$sN + 4`. Deciding whether the guest really passes `4` needs the
sequence, not the last observation.

## Decision

`EeHleState` keeps a fixed 16-entry ring of `EeSyscallObservation` values, each
capturing the pre-dispatch boundary:

- `pc`, normalized service `number`, `$a0..$a3`, `$v0` before the call, and
- `path`: `guest_handler`, `host_service`, or `unhandled`.

`dispatch_ee_syscall` writes the observation after the existing boundary fields
are populated and before any handler runs, so a handler's side effects cannot
contaminate the record. The ring is addressed with `syscall_observation(index)`,
counting from the oldest retained entry; `syscall_sequence_count` saturates at
the capacity.

The generated native probe prints `syscall_seq_count` and `syscall_seq` as
`pc:number:path:a0:a1:a2:a3:v0` records joined by `;`, where the path is `G`,
`H` or `U`. No allocation, no per-call I/O, and no effect on guest state.

## Consequences

- A bounded run can now be read as a short call sequence, including transitions
  between host services and guest handlers.
- The bound is fixed at 16 so the runtime stays allocation-free; longer
  sequences retain only the tail. Raises the capacity is a separate decision.
- The ring is diagnostic state. Nothing in dispatch depends on it, so disabling
  or ignoring it cannot change guest-visible behaviour.

## Evidence

- `reps2_hle_syscall_codegen_smoke` grew to 31 cases: one checks ring
  saturation and eviction order across host-service dispatches, and the
  existing guest-handler test now also asserts that the recorded path is
  `guest_handler` with the pre-dispatch `$a0`/`$a1`.
- `native_build::tests::probe_reports_hle_accounting_and_enables_the_layer_on_request`
  asserts the generated probe source contains `syscall_seq_count`.
