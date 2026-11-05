# ADR 0068: resolve EE FindAddress HLE (deferral condition satisfied)

- **Status:** Resolved (supersedes the deferral)
- **Date:** 2026-11-05
- **Scope:** M4 real-title boundary

## Context

After SetSyscall registration, Downhill Domination reaches EE service `0x83`
(`FindAddress`) at `PC=0x0025EF7C` with `$a0=0x80000000`,
`$a1=0x80080000`, `$a2=0x0025EF38`, `$a3=0x0029E580`. Black reaches the same
service at `PC=0x0036D664` with `$a0=0x80000000`, `$a1=0x80080000`.

## Evidence

ps2sdk assigns service `0x83` the name `FindAddress` in `syscallnr.h`, but its
public `kernel.h` exposes no declaration or ABI for it. The available ps2sdk
headers expose `GetEntryAddress(int syscall)` (service `0x5B`), which is a
different operation. Public ps2tek material inspected for EE syscall services
does not define the `FindAddress` argument contract, return value, memory effects,
or whether the service searches a syscall/vector table, exception handlers,
or another kernel address registry.

The deferral condition was satisfied by an independent behavioral reference
(PS2Recomp commit `9e4fd79`, `ps2_syscalls_system.inl`, research only — no source
copied): `FindAddress(table_start, table_end, target)` takes `$a0` = table start
(inclusive), `$a1` = table end (exclusive), `$a2` = target address, scans word
entries in `[a0, a1)`, and returns the guest address of the first matching word
or `0` when none matches. KSEG0/KSEG1 aliasing applies (`addr & 0x1FFFFFFF`),
reads are memory-validated, and an unreadable entry ends the scan. The contract
was cross-checked against both observed call sites (Downhill Domination
`0x0025EF78` = `li $v1, 0x83; syscall;` and Black `0x0036D664`), both passing a
kernel window `0x80000000..0x80080000` and a code address as the target.

## Decision

Implement `FindAddress` (0x83) as an honest host service (`hle_find_address`)
dispensed through the existing shared syscall dispatch table (`kEeSyscallServices`,
sorted by number, guest-handler slots take precedence). It returns the guest
address of the first matching word entry or `0` ("not found") for an empty,
inverted, or unpopulated window; it writes nothing to guest memory. REPS2 does
not populate a kernel table, so a retail title whose window contains no matching
entry observes `0` and must handle that result itself — no faked kernel success.

The service is registered under `kEeSyscallFindAddress = 0x83U` with observation
fields on `EeHleState` (`find_address_called`, `last_find_address_start/end/
target/result`). Six tests cover match, KSEG-alias match, no match, empty/inverted
window, unreadable memory, and dispatch-path resumption; the HLE smoke suite grew
from 23 to 29 cases. See `runtime/include/reps2/runtime/ee_hle.hpp`,
`runtime/include/reps2/runtime/ee_state.hpp`, `tests/hle_syscall_smoke.cpp`.

## Reusable diagnostic boundary

The HLE state records the pre- and post-service `$v0` values and the exact
resume PC for every handled service. This is host-side observation only; it does
not claim a FindAddress result or memory side effect. The fields are reset with
`initialize_ee_state` and remain available to an optional probe without changing
normal syscall semantics.

## Next evidence required

- Confirmation of the caller's subsequent branch and use of the `FindAddress`
  result (whether `0` vs. a match address changes title behavior).
- A repeatable guest-visible boot stage past the `0x0025EF7C` / `0x0036D664`
  boundary, verified with the bounded probe and HLE enabled.

No visible boot or playability claim follows from the current boundary.

## Evidence

ps2sdk assigns service `0x83` the name `FindAddress` in `syscallnr.h`, but its
public `kernel.h` exposes no declaration or ABI for it. The available ps2sdk
headers expose `GetEntryAddress(int syscall)` (service `0x5B`), which is a
different operation. Public ps2tek material inspected for EE syscall services
does not define the `FindAddress` argument contract, return value, memory effects,
or whether the service searches a syscall/vector table, exception handlers,
or another kernel address registry.

The call's first argument is a kernel-segment address and the second is another
kernel-segment address; the remaining arguments point into the title's code/data
region. These values alone do not identify a safe interpretation.

## Decision

Do not implement `FindAddress` as a success stub or return one of the observed
arguments. Preserve `Status::unhandled_syscall` until an independent ABI source,
trace of the return value and memory effects, or a minimal guest reproduction
establishes its contract. The SetSyscall registration remains host-side state;
it does not justify invoking arbitrary guest handlers or treating FindAddress as
handler lookup.

## Reusable diagnostic boundary

The HLE state now records the pre- and post-service `$v0` values and the exact
resume PC for every handled service. This is host-side observation only; it does
not claim a FindAddress result or memory side effect. The fields are reset with
`initialize_ee_state` and remain available to an optional probe without changing
normal syscall semantics.

## Additional bounded execution evidence

A regenerated MSVC retail probe with `/bigobj` executed with budget `2,757,047` and
`REPS2_INSTRUCTION_TRACE=1`. It reached the same unhandled service boundary:
`status=14`, `PC=0x0025EF7C`, service `0x83`, with `7` syscall observations and
`6` handled services. The rolling window reported `16` PCs, but the generated
probe recorded the initial-entry window (`0x0010A008` through `0x0010A040`)
because tracing occurred before the generated block updated `state.pc`. This
ordering defect was fixed in `ec2becc`. After regeneration, the corrected
window was:
`0x0025EFAC,0x0025EFB0,0x0025EFB4,0x0025EFB8,0x0025EFBC,0x0025EFC0,`
`0x0025EFC4,0x0025EFC8,0x0025F090,0x0025EFD0,0x0025EFD4,0x0025F090,`
`0x0025EFDC,0x0025EFE0,0x0025EFE4,0x0025EF78`.
The window reaches the syscall-adjacent caller path but does not include the
post-service path because execution stops at the unhandled boundary. Generated
caller code shows the preceding sequence saves registers at `$sp+0x00` through
`$sp+0x70`, loads `$a0` from `[$a2+0x2218]`, loads `$a1` from `[$s0+4]`, sets
`$v1=0x83`, and executes `SYSCALL`; the delay slot is at `0x0025EF80` and the
following block returns through `$ra`. This identifies the call-site shape but
not the service's return or memory contract. It provides control-flow context
only; it does not establish the `FindAddress` ABI.

## Next evidence required

- A public or independently reconstructed declaration/implementation.
- Return register and guest-memory comparison before and after the call.
- The caller's subsequent branch and use of the result.
- Confirmation of whether the two kernel-segment arguments are source/destination
  ranges, vector bases, or unrelated kernel objects.

No visible boot or playability claim follows from the current boundary.
