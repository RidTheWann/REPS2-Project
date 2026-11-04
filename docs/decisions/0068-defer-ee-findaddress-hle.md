# ADR 0068: defer EE FindAddress HLE

- **Status:** Accepted
- **Date:** 2026-11-05
- **Scope:** M4 real-title boundary

## Context

After SetSyscall registration, Downhill Domination reaches EE service `0x83`
(`FindAddress`) at `PC=0x0025EF7C` with `$a0=0x80000000`,
`$a1=0x80080000`, `$a2=0x0025EF38`, `$a3=0x0029E580`.

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
post-service path because execution stops at the unhandled boundary. It provides
control-flow context only; it does not establish the `FindAddress` ABI.

## Next evidence required

- A public or independently reconstructed declaration/implementation.
- Return register and guest-memory comparison before and after the call.
- The caller's subsequent branch and use of the result.
- Confirmation of whether the two kernel-segment arguments are source/destination
  ranges, vector bases, or unrelated kernel objects.

No visible boot or playability claim follows from the current boundary.
