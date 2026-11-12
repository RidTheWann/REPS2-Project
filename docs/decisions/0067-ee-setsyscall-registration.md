# ADR 0067: EE SetSyscall registration boundary

- **Status:** Accepted; amended by ADR 0072, which also publishes the handler
  pointer into the guest-visible syscall table.
- **Date:** 2026-11-05
- **Scope:** opt-in EE syscall HLE layer

## Context

After CreateSema, Downhill Domination reaches service `0x74` at
`PC=0x0025F094` with `$a0=0x83`, `$a1=0x0025EF38`, `$a2=0` and
`$a3=0x0029E580`. ps2sdk declares `void SetSyscall(s32 syscall_num, void *handler)`.
The wrapper places the service number in `$v1`, while the call arguments remain
in `$a0` and `$a1`.

## Evidence

- ps2sdk `kernel.h` declares `SetSyscall(s32 syscall_num, void *handler)` and
  `GetSyscallHandler(s32 syscall_no)`.
- ps2sdk `syscallnr.h` assigns `SetSyscall` service `0x74`.
- ps2sdk `kernel.S` uses the ordinary `li $3, __NR_SetSyscall; syscall` wrapper.
- The ps2sdk privileged dispatch helper indexes a kernel syscall vector with the
  registered slot and jumps to the stored address. ps2tek describes syscall
  patching as replacing a syscall slot with a handler address.
- The sources do not establish handler ABI details, replacement failure codes,
  whether every slot accepts every address, or whether invalid registrations
  write an error result. `SetSyscall` is declared `void`.

## Decision

Implement the safe registration subset only:

1. Accept slots `0..255`, matching ps2sdk's 256-entry syscall-vector model.
2. Require a nonzero, word-aligned handler address resolving through the existing
   EE main-RAM address aliases. Reject invalid slots and handlers without
   changing the registration table.
3. Record the handler address in host-only runtime state and mark the slot active.
   Re-registering a slot replaces its recorded address.
4. Return `Status::ok` and leave guest registers and memory unchanged. The guest
   API is `void`; no success or failure value is fabricated in `$v0`.
5. Do not call arbitrary guest handler code yet. Handler invocation requires a
   guest-call ABI, saved context, return continuation and exception/scheduler
   semantics not established by the current runtime. An unknown subsequent
   service remains `unhandled_syscall`.

## Validation

Native generated-code tests cover valid registration, replacement state,
invalid slot, null, misaligned and unmapped handler addresses, register
preservation and no guest-memory mutation. Downhill Domination now handles
SetSyscall and reaches the next unhandled service `0x83` at `PC=0x0025EF7C`.
The bounded trace is repeatable; no handler execution or boot claim follows.

## References

- https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/kernel.h
- https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/syscallnr.h
- https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/src/kernel.S
- https://psi-rockin.github.io/ps2tek/
