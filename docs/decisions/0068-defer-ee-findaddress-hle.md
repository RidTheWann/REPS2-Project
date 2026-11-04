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

## Next evidence required

- A public or independently reconstructed declaration/implementation.
- Return register and guest-memory comparison before and after the call.
- The caller's subsequent branch and use of the result.
- Confirmation of whether the two kernel-segment arguments are source/destination
  ranges, vector bases, or unrelated kernel objects.

No visible boot or playability claim follows from the current boundary.
