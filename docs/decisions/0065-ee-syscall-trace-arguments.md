# ADR 0065: capture EE syscall boundary arguments

- **Status:** Accepted
- **Date:** 2026-11-04
- **Scope:** host-only HLE accounting and generated native probe

## Context

The aggregate HLE trace identified Downhill Domination's next boundary as
`CreateSema` (0x40), but it did not show the register arguments or the guest
memory referenced by `$a0`. That was insufficient to distinguish a documented
kernel call from a call whose parameter structure was not initialized.

The next service must not return a fabricated semaphore ID. Public sources say
`CreateSema(ee_sema_t*)` returns an ID on success and `-1` on failure, but they do
not establish the complete validation, object lifetime or scheduler behavior.

## Decision

Record the following host-only fields at every HLE dispatch boundary before the
handler runs:

- trapping guest `pc`;
- raw and normalized service number (existing fields);
- `$a0..$a3`;
- `$v0` before dispatch;
- `$sp` (`$r29`).

Expose them in the machine-readable native probe trace as `hle_pc`,
`hle_a0..hle_a3`, `hle_v0_before` and `hle_sp`. Existing aggregate counters and
service name remain unchanged. `initialize_ee_state` already resets the entire
host observation structure, so no guest state is added.

## Validation

- Native and Rust tests assert the generated trace format and continue to pass.
- Downhill Domination repeated bounded run:

```
entry=0x0010A008 status=14 stage=unhandled_syscall pc=0x0025A6E4 executed=2756997
syscalls=3 hle_handled=2 hle_pc=0x0025A6E4 hle_number=0x00000040
hle_v0_before=0x00000001 hle_a0=0x0093C7E0 hle_a1=0xFFFFFFFF
hle_a2=0x00000000 hle_a3=0x0029E580 hle_sp=0x0093C7E0
```

  (`a3` is included in the exact trace; the line is wrapped here only for
  readability.) The 24 bytes at `0x0093C7E0` in the initial RAM image are all
  zero. Under the public `ee_sema_t` layout this means `count=0`,
  `max_count=0`, `init_count=0`, `wait_threads=0`, `attr=0`, `option=0`.
- Rust fmt, clippy, workspace tests, CMake Release build and all 30 CTest cases
  pass.

## Consequences

The next service can be classified from reproducible guest state instead of an
aggregate counter. The trace is host diagnostics, not a claim that a BIOS or
kernel ran. The new fields do not implement `EndOfHeap`, the unknown `0x3F`
slot, `CreateSema`, scheduling or semaphore object lifetime. Those remain
explicit runtime boundaries until their behavior is evidenced.
