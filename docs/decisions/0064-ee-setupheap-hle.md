# ADR 0064: conservative EE SetupHeap HLE

- **Status:** Accepted
- **Date:** 2026-11-04
- **Scope:** opt-in EE syscall HLE layer

## Context

After ADR 0063 made the continuation after a serviced `SYSCALL` translatable,
Downhill Domination progressed past its first service:

```
li $v1, 0x3D
syscall                         # 0x0010A194
```

The observed arguments are `heap_start = 0x008A0400` and
`heap_size = 0xFFFFFFFF`. The previous runtime stopped honestly at
`Status::unhandled_syscall`, so the next narrow service boundary was
`SetupHeap` (0x3D).

Public sources disagree on the placement of the `-1` sentinel. ps2sdk declares
`SetupHeap(void *heap_start, s32 heap_size)` and its startup code calls
`SetupHeap(&_end, -1)`. ps2tek documents a sentinel on the heap address and says
the heap ends at the current thread's stack pointer. The retail call and ps2sdk
startup both put `-1` in the size slot. The sources do not prove that both forms
are accepted by the hardware.

The sources also disagree about terminology for `EndOfHeap`'s result. ps2tek's
SetupHeap entry says it returns the heap end, while its 0x3E wording calls it the
base. ps2sdk's `_sbrk` uses `EndOfHeap()` as the upper bound, so this runtime
selects the end interpretation. ps2sdk declares `SetupHeap` as `void`, and its
startup discards the machine-level result.

## Decision

1. Add service 0x3D to the existing sorted EE HLE table. Do not add a second
   syscall mechanism or an allocator.
2. For an explicit non-sentinel `(heap_start, heap_size)`, compute
   `heap_end = heap_start + heap_size` with defined unsigned 32-bit wraparound,
   record the range in host-only `EeHleState`, and return `heap_end` in `$v0`.
   This is the documented arithmetic for the normal form; range validation,
   alignment and overlap policy remain outside this service until evidence
   requires them.
3. For the observed `heap_size == 0xFFFFFFFF` form, use the stack pointer
   recorded by `SetupThread` as `heap_end`. This is an explicitly named
   compatibility extension based on ps2sdk startup and the retail trace, not a
   claim that the hardware contract is settled. The service records the raw
   arguments and selected end for traceability.
4. Do not implement `heap_start == 0xFFFFFFFF` as an additional alias yet. It is
   documented by ps2tek but conflicts with the parameter placement used by
   ps2sdk and the observed title; accept no unresearched combinations.
5. Do not synthesize failure codes, heap metadata, malloc behavior, stack slack,
   alignment rounding, overlap rejection, repeated-call semantics or an EE
   `CreateHeap`. Unsupported services continue to stop as
   `Status::unhandled_syscall`.
6. The service's `$v0` result is chosen from ps2tek's machine-level description
   even though ps2sdk's C API returns `void`; ordinary ps2sdk callers discard it.

## Evidence

- ps2sdk `kernel.h`: `SetupHeap(void *heap_start, s32 heap_size)`:
  https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/kernel.h
- ps2sdk syscall numbers and wrapper:
  https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/syscallnr.h
  and https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/src/kernel.S
- ps2sdk startup order and `SetupHeap(&_end, -1)`:
  https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/startup/src/crt0.c
  and https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/startup/src/linkfile
- ps2tek BIOS EE syscall list and thread model:
  https://psi-rockin.github.io/ps2tek/

## Validation

- Native smoke coverage checks explicit range arithmetic, the observed
  `heap_size=-1` stack-end extension, result preservation through the generated
  continuation, and table registration.
- The Downhill Domination probe is rerun with a bounded budget after rebuilding
  the generated project. Its next boundary must be a later syscall or a
  different structured runtime stop; a positive counter or handled call alone
  is not a boot claim.
- Rust and CMake/CTest gates remain required.

## Consequences and limitations

`SetupHeap` no longer blocks the observed Downhill Domination startup boundary,
but this does not model a kernel thread object or newlib allocator. The next
real-title event determines whether `EndOfHeap`, thread creation, interrupts,
GS setup or another service is required. No visible boot stage or playability
claim follows.
