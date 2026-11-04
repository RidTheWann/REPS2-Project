# ADR 0062: EE syscall HLE dispatch and the SetupThread service

- **Status:** Accepted
- **Date:** 2026-11-04
- **Scope:** C++ runtime EE syscall layer (`runtime/include/reps2/runtime/ee_hle.hpp`), SYSCALL emission in the control-flow code generator, and the generated native probe

## Context

Before this change a guest `syscall` could only become an architectural Level-1
exception. The generated block set `state.pc` to the trapping PC and returned
`raise_ee_exception(state, ExceptionCode::SystemCall)`, which writes
EPC/BD/Cause and transfers control to V_COMMON (`0x80000180`, or
`0xBFC00380` with BEV). The linear entry scan over the retail images already
stops at the first `SYSCALL` trap, and the generated native probe reports it as
`stage=guest_exception` with `exccode=8` and the service number the SDK loads
into `$v1`.

That is faithful to the instruction and useless for the M4 goal. V_COMMON is
the title's kernel — BIOS code that REPS2 neither ships nor reimplements — so a
real title's boot path stalls at its very first kernel call, which for ps2sdk
crt0 is `SetupThread` (0x3C). The runtime therefore needs an explicit,
auditable boundary between "the guest entered the kernel" and "a host service
answered the kernel call", without pretending a kernel exists when it does not.

The evidence for the calling convention and for SetupThread is recorded in
[RESEARCH.md](../RESEARCH.md#ee-kernel-syscall-interface-and-setupthread). In
short: the service number is in `$v1` (GPR[3]) — ps2sdk emits
`li $3, __NR_x; syscall` — arguments use `$a0..$a3`, the result returns in `$v0`,
the "i" wrappers pass the negated number, and `SetupThread`'s documented result
is the initial stack pointer that crt0 immediately moves into `$sp`.

## Decision

1. Add a host HLE state block to `EeState`: `EeHleState` with an opt-in
   `syscall_enabled` flag, dispatch accounting (`syscall_count`,
   `syscall_handled_count`, `last_syscall_code`, `last_syscall_number`) and the
   SetupThread observations. None of it is guest-architectural state, it is not
   part of any guest-visible register, and `initialize_ee_state` resets it so a
   session must opt in explicitly. The existing `instructions_executed`
   accounting is host state for the same reason.
2. Normalize the dispatch number from `$v1` as a 32-bit value and use its
   magnitude, so the ps2sdk "i" variants dispatch to the same service without a
   second table. Unsigned subtraction avoids the `INT32_MIN` negation that
   signed arithmetic cannot represent.
3. Route every guest `syscall` through `dispatch_ee_syscall(state, resume_pc,
   in_branch_delay)` and define exactly four outcomes:
   - layer disabled: raise the architectural Level-1 exception, byte-for-byte
     the behavior that existed before this ADR;
   - enabled, no handler for the number: return
     `Status::unhandled_syscall` with PC still on the SYSCALL and no COP0
     context written, so the caller reports the exact missing service;
   - enabled, handler returns `Status::ok`: count the service and continue at
     `resume_pc`;
   - handler returns another status: propagate it unchanged.

   A handler may not report success on behalf of a service the layer does not
   implement; `Status::unhandled_syscall` is added for that case and is named
   `unhandled_syscall` in the stable probe-stage contract.
4. Make the code generator pass the resume PC explicitly instead of deriving it
   in the runtime, because the correct continuation depends on the site:
   - a `GuestException` terminator resumes at `guest_pc + 4`;
   - a SYSCALL in a conditional-branch delay slot resumes at the branch target
     or the fallthrough, so the predicate is now materialized for such slots
     even under `DelaySlotPolicy::ExecuteAlways`, and the emitted resume is
     `(branch_taken ? target : fallthrough)`;
   - a SYSCALL in a direct jump delay slot resumes at the jump target, and in a
     register-indirect jump or return delay slot at the captured dynamic target.

   BREAK keeps the architectural exception unchanged: it has no service
   semantics.
5. Implement two services, each with the evidence it rests on:
   - `SetupThread` (0x3C) decodes `gp`, `stack`, `stack_size` and `args` from
     `$a0..$a3`, honors the `0xFFFFFFFF` "no caller stack" sentinel by placing
     the stack below the top of main RAM minus a 4 KiB guard, subtracts the
     observed `0x2A0` reserve, returns the resulting stack pointer in `$v0`
     (sign-extended into the 64-bit lane like any 32-bit EE result) and clears
     `$v1`. It records the decoded arguments and the returned pointer in
     `EeHleState`. It deliberately does **not** create a thread control block,
     rewrite the argv block, consume `root_func` or schedule anything: those
     need an EE-side kernel object model and a scheduler, and faking them would
     hide the next missing service instead of naming it.
   - `FlushCache` (0x64) returns `$v0 = 0`. The runtime has no instruction-cache
     model and the host is coherent, so this is an accurate description of the
     host rather than a fabricated kernel behavior; it becomes a real service
     only if a cache model appears.
6. Expose the layer through the developer CLI and the probe: `--hle-syscalls` on
   `recompile-elf` and `compile-cfg-fixture` bakes `state.hle.syscall_enabled =
   true` into the generated probe, whose trace line gains `hle`, `syscalls`,
   `hle_handled`, `hle_number` and `hle_name`. The default stays disabled, so
   every existing translation and test keeps the architectural path.

## Validation

- Control-flow codegen unit tests pin the emitted dispatcher call for a SYSCALL
  terminator (`resume = pc + 4`), a SYSCALL in a branch delay slot (predicate
  materialized, `resume = taken/fallthrough ternary`) and a SYSCALL in a return
  delay slot (`resume = return target`), and confirm no
  `ExceptionCode::SystemCall` is emitted any more.
- The native fixture gains three entries (`0xE000` syscall + resume marker +
  return, `0xE100` unhandled service, `0xE200` branch with a SYSCALL in its delay
  slot). The new `reps2_hle_syscall_codegen_smoke` test drives ten cases on that
  generated code on Windows/MSVC:
  - disabled layer: `exception_delivered`, V_COMMON, ExcCode 8, EPC at the
    SYSCALL, BD clear, EXL set, zero HLE accounting;
  - SetupThread with the sentinel: stack top = 32 MiB − 4 KiB, returned pointer =
    top − `0x2A0`, arguments recorded, `$v0` overwritten by the resumed
    instruction (proving the resume PC), no COP0 context;
  - SetupThread with an explicit stack: top = base + size;
  - negated service number: `last_syscall_code = -0x3C`,
    `last_syscall_number = 0x3C`, service handled;
  - unknown number: `unhandled_syscall`, PC at the SYSCALL, guest `$v0` and all
    COP0 registers untouched, accounting 1 seen / 0 handled;
  - FlushCache: handled, resume marker ran, no thread state invented;
  - delay-slot SYSCALL taken: resumes at the taken target (result 9, not 7);
  - delay-slot SYSCALL not taken: resumes at the fallthrough (result 7);
  - delay-slot SYSCALL with the layer disabled: architectural exception with EPC
    at the branch and BD set;
  - service table: `SetupThread`/`FlushCache` resolve, `SetupHeap` (0x3D) does
    not, and `status_name(unhandled_syscall) == "unhandled_syscall"`.
- `native_build` unit tests assert the enabled/disabled probe source and every
  new trace field; the CLI parse tests assert `--hle-syscalls` is opt-in for both
  commands.
- The generated probe was configured, built and executed with
  `--native-build --probe-budget 64 --hle-syscalls`, and its trace line reports
  `hle=1 syscalls=0 hle_handled=0 hle_number=0x00000000 hle_name=-` for a
  translation whose entry never reaches a SYSCALL.
- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` and the full CMake/CTest suite
  (30 tests, Release, Windows/MSVC) pass.

## Consequences

- A real title's first kernel call now has a place to be answered, and an
  unanswered call is reported as a named host limitation with the exact service
  number instead of as a fictitious guest exception or a silent success.
- SetupThread is intentionally partial. A title that needs a real thread object,
  the argv rewrite, `root_func` execution or `SetupHeap` will stop at that
  service with `unhandled_syscall`, which is the honest next-work signal.
- No real-title run is claimed by this change: no lawfully owned executable is
  available in the repository or CI, so the evidence here is the synthetic
  native fixture plus public documentation. Binding HLE behavior to a title
  digest, ELF revision and probe trace remains an M4 task, and the probe fields
  added here are what that run will record.
- The layer is host configuration, not guest state: a session that enables it
  changes no guest-visible bit except the ones a serviced call legitimately
  writes (`$v0`, `$v1` for SetupThread, PC).
