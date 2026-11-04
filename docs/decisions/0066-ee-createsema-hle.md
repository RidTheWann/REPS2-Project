# ADR 0066: EE CreateSema HLE with a bounded semaphore table

- **Status:** Accepted
- **Date:** 2026-11-04
- **Scope:** opt-in EE syscall HLE layer

## Context

ADR 0064 added `SetupHeap` (0x3D), and the next observed real-title boundary was
the first semaphore service:

```
# Downhill Domination USA (SCUS_971.77), bounded probe
li $v1, 0x40
syscall                         # 0x0025A6E4
```

The captured arguments were `$a0 = 0x0093C7E0` (equal to `$sp`), `$a1 = -1`,
`$a2 = 0`, `$a3 = 0`. The 24 bytes at `$a0` in the *initial* RAM image are zero,
which proves nothing about the call: a stack-allocated parameter block is only
populated by guest code at run time. Implementing the service therefore required
a documented parameter layout and a decision about how much of a kernel object to
model without a scheduler.

## Research

Documented facts:

- ps2sdk `kernel.h` declares `s32 CreateSema(ee_sema_t *sema)` and service 0x40
  in `syscallnr.h`, and defines
  `typedef struct { int count, max_count, init_count, wait_threads; u32 attr, option; } ee_sema_t;`
  — six 32-bit fields, 24 bytes.
- ps2sdk documents `MAX_SEMAPHORES 256` as the kernel limit.
- The public EE syscall description states: "Creates a semaphore. Returns the
  semaphore's id if successful and -1 if not. Only `s->init_count` and
  `s->max_count` need to be specified. `s->attr` and `s->option` may also be
  specified." The internal kernel object is described as
  `{ free, count, max_count, attr, option, wait_threads, wait_next, wait_prev }`,
  and the same source marks the guest `attr`/`option` words as not interpreted.
- `WaitSema` blocks a thread when `count` is zero, `SignalSema` increments
  `count` (or releases a waiter) and both force a reschedule; `PollSema`
  decrements and fails when the count is zero.

Unresolved:

- No public source states the kernel's semaphore-id allocation order, whether the
  kernel rewrites the guest parameter block, or which count pairs it rejects.
- The EE kernel services are not the IOP THSEMAP services; their equivalence is
  not demonstrated, so nothing here is derived from the IOP side.

## Decision

1. Add service 0x40 to the existing sorted EE HLE table. Do not add a second
   dispatch mechanism.
2. Read the documented 24-byte parameter block from EE main RAM through the
   existing address-resolution path, so aliases, alignment and bounds behave
   exactly as a guest load would. Only `init_count` and `max_count` are
   interpreted; `count` and `wait_threads` are read and ignored because they are
   the fields `ReferSemaStatus` reports back.
3. Refuse, with the documented `-1` result and no allocation, when the parameter
   block is not word-aligned or not readable for its full width, when either
   count is negative, when `init_count > max_count`, or when the 256-object table
   is full. The refusal reason is recorded host-side (`EeSemaRejection`); it is
   never invented as a new guest error code.
4. On success write the new id to `$v0` and record the object: count,
   max_count, attr, option and the creating PC. Ids are indices into a fixed
   256-entry host table, so a title cannot allocate an unbounded number of
   objects, and ids are unique for the life of a session.
5. Id allocation order (lowest free slot) is documented as a host choice, because
   only uniqueness and the table bound are evidenced.
6. Do not implement `DeleteSema`, `SignalSema`, `WaitSema`, `PollSema` or
   `ReferSemaStatus` yet. A waiter list is meaningless without a scheduler range:
   nothing can block, and nothing can be released from a block. Those services
   keep stopping as `Status::unhandled_syscall` until the observed path needs
   them and their blocking semantics can be either implemented or refused
   explicitly.

## Implementation boundary

- Created state only: an object with a count, a maximum, its attribute words and
  its creating PC. No waiter list, no dispatcher interaction, no ownership by a
  thread, no write-back to the guest parameter block.
- No allocator, no reference counting, no inherited-heap or thread interaction.
- The service is inside the opt-in layer; with the layer disabled the
  architectural Level-1 `SYSCALL` exception is delivered unchanged.

## Evidence

- ps2sdk `kernel.h` (`ee_sema_t`, `CreateSema`, `MAX_SEMAPHORES`):
  https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/kernel.h
- ps2sdk `syscallnr.h` (`__NR_CreateSema 0x40`):
  https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/syscallnr.h
- ps2sdk `kernel.S` (the `li $3, __NR_CreateSema; syscall` wrapper shape):
  https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/src/kernel.S
- ps2tek (SemaParam layout, semaphore object, `40h` result wording, semaphore
  scheduling behavior): https://psi-rockin.github.io/ps2tek/

## Validation

- Native smoke coverage through generated code: id allocation and a second
  distinct id, `init_count`/`max_count` interpretation with `count` and
  `wait_threads` ignored, negated service number resolution, refusal of
  `init_count > max_count`, refusal of negative counts, refusal of misaligned,
  out-of-RAM and out-of-bounds parameter pointers, the 256-object bound, `$v0`
  result preservation through the generated continuation, no `$v1`/`$a0`/COP0
  side effects, and an unchanged guest parameter block.
- Real title, repeatable: Downhill Domination USA (`SCUS_971.77`, local ISO).
  Before this change the probe stopped at `CreateSema` with
  `status=unhandled_syscall`, `pc=0x0025A6E4`. After it, the same bounded run
  services the call twice and reaches a later boundary, syscall `0x74`
  (`SetSyscall`) at `pc=0x0025F094`:
  `executed=2757032 syscalls=5 hle_handled=4 hle_number=0x00000074`,
  `sema_calls=2 sema_live=2 sema_init=1 sema_max=1 sema_refused=none`.
  Two consecutive runs produced byte-identical trace lines.
- Disc provenance for that run: ISO SHA-256
  `0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`,
  extracted ELF SHA-256
  `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`,
  ELF entry `0x0010A008`, 28,300 blocks, 146,425 represented instructions,
  `--tu-count 8` (the single generated translation unit exceeds the MSVC object
  section limit and does not compile without splitting).
- Rust and CMake/CTest gates are required as always.

## Consequences and limitations

The guest now receives a real id, not a fabricated one, and reads it exactly as
the documented API describes. Nothing can wait on the object, so a title that
depends on semaphore blocking will stop at the corresponding service instead of
running with an unfinished scheduler. The next observed boundary is `SetSyscall`
(0x74), which installs a guest handler for a syscall number and therefore needs a
dispatch-table model that can call guest code — a substantially larger contract
than this change, and not implemented here. No visible boot stage, no BIOS
completion and no playability claim follows from this ADR.
