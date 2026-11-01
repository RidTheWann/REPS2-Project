# ADR 0033: EE Level-1 SYSCALL/BREAK exception delivery and ERET

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** EE COP0 exception state, control IR, C++ control-flow lowering, native smoke coverage, and build-manifest schema v2

## Context

ADR 0031 separated decoded SYSCALL/BREAK instructions from genuinely unknown encodings, but still returned terminal `Status::syscall` and `Status::breakpoint` values with the PC left on the trapping instruction. That was useful diagnostic classification, but it did not implement the architectural exception transition expected by guest code.

The EE exception path needs coordinated state changes rather than a status-only result: Cause.ExcCode, EPC, the branch-delay (BD) marker, Status.EXL, and the selected common exception vector. Exception entry also has a nested-exception rule: when EXL is already set, the original EPC/BD restart frame must survive. ERET must then select the saved return PC using the exception-level state and clear the corresponding level bit.

The instruction decoder already recognizes SYSCALL, BREAK and ERET, and the analyzer deliberately makes them block terminators. Implementing them in the runtime and code generator is a tractable vertical slice toward real EE execution, without pretending to provide BIOS services, a complete interrupt controller, or all exception causes.

## Architectural evidence

The implementation follows the publicly available Sony *Emotion Engine Core User's Manual*:

- Chapter 4.1.2 defines the Level-1 exception entry behavior, EPC/BD recording, EXL handling, and BEV-selected common vector.
- Sections 4.2.11 and 4.2.12 assign exception code 8 to SYSCALL and 9 to BREAK.
- The ERET description documents return through the saved exception context and warns that simultaneous EXL and ERL do not form a supported return condition.

Independent cross-check: the PS2Tek COP0 reference documents the Cause.ExcCode and BD fields, exception-level state, exception vectors, EPC/ErrorEPC, and ERET return selection.

References:
- Sony, *Emotion Engine Core User's Manual*, chapter 4: https://docs.alexrp.com/mips/ee.pdf
- PS2Tek, COP0 / exception reference: https://psi-rockin.github.io/ps2tek/

## Decision

### 1. Separate architectural exceptions from unsupported operations

The typed control IR now has three semantically distinct cases:

- `GuestException { guest_pc, trap }` for SYSCALL and BREAK.
- `ExceptionReturn { guest_pc }` for ERET.
- `Unsupported { guest_pc, mnemonic }` only for an encoding or scalar operation the current implementation cannot lower.

This is not merely a diagnostic rename. The first two cases emit executable runtime transitions. Consequently, `unsupported_count()` excludes known guest exceptions, while `trap_count()` counts the typed exception-entry terminators.

### 2. Level-1 exception entry

The runtime exposes `raise_ee_exception(EeState&, ExceptionCode, bool in_branch_delay)`.

- SYSCALL writes Cause.ExcCode = 8; BREAK writes Cause.ExcCode = 9.
- When Status.EXL is clear, EPC becomes the faulting PC (or PC-4 for a branch-delay exception), Cause.BD is set or cleared accordingly, and Status.EXL is set.
- When Status.EXL is already set, a nested Level-1 exception updates ExcCode but preserves the original EPC and BD frame.
- With Status.BEV set, execution transfers to the bootstrap common vector `0xBFC00380`; otherwise it transfers to the common vector `0x80000200`.
- The helper returns `Status::exception_delivered` so the native host knows that an exception transition occurred.

The current CFG rejects traps in delay slots, as it has no representation for that control-flow case. The helper's BD/EPC rule is nevertheless directly unit-tested and is ready for a future delay-slot exception terminator.

### 3. ERET return

ERET is lowered to a dynamic exception-return terminator. The runtime helper:

- If EXL is set and ERL is clear, restores PC from COP0.EPC and clears EXL.
- If ERL is set and EXL is clear, restores PC from COP0.ErrorEPC and clears ERL.
- If neither or both bits are set, returns `Status::invalid_exception_return` without changing the PC.

Rejecting ambiguous level state avoids inventing a priority where the EE manual explicitly warns that EXL+ERL cannot operate properly. After a successful ERET, the generated CFG dispatcher can continue only if the restored PC is in its translated block table; otherwise it returns the normal external-control-flow boundary to the host.

### 4. Host-service boundary

`Status::exception_delivered` means exception state has been populated and `EeState::pc` points at the selected vector. It does **not** mean the vector handler ran, a BIOS syscall was serviced, or a guest service succeeded. A host session layer may resume execution at that PC when it has a suitable translated handler, or provide a future explicitly modeled HLE service boundary. No fake success path is introduced.

### 5. Build-manifest schema v2

Manifest schema v1's `unsupported_stop_count` used to include SYSCALL/BREAK because they were terminal unsupported stops. That meaning must not silently change. Schema v2 adds `guest_exception_count` and uses `unsupported_stop_count` only for genuinely unsupported terminators. Older v1 artifacts remain clearly identified by their own `manifest_version`; newly generated artifacts use v2. The deterministic/timestamp-free and `playable: false` invariants remain unchanged.

## Validation

The implementation adds or updates:

- Rust control-IR tests for distinct GuestException, ExceptionReturn, and Unsupported variants and for their counts.
- Rust codegen tests for Level-1 entry helper emission and dynamic ERET emission.
- Build-manifest tests that assert schema v2 and independent exception/unsupported totals.
- Native generated-C++ tests for SYSCALL/BREAK ExcCode values, EPC, EXL, normal and BEV vectors, nested exception preservation, branch-delay EPC/BD helper behavior, ERET resumption through EPC, ERL/ErrorEPC return, and invalid/ambiguous return state.
- Existing Windows/MSVC and Linux/GCC CI remains the required cross-platform gate.

The native fixtures are synthetic and prove isolated state transitions; they do not establish retail-title compatibility or playability.

## Scope and limitations

This increment does not implement BIOS or kernel HLE services, other synchronous exception causes, TLB refill/translation, bus-error delivery, Level-2 events, asynchronous interrupts, timer/DMA interrupt delivery, or branch-delay trap integration in the static CFG. It does not claim that the selected exception vector or its handler is available in every compiled title. The full PS2 runtime and all compatibility gates remain incomplete.

## Consequences

- Guest SYSCALL/BREAK now produce an architectural Level-1 state transition rather than a terminal fake service result.
- ERET can restore a dynamic saved guest PC and allow the generated dispatcher to continue when the target block is present.
- Coverage statistics no longer classify modeled guest exceptions as unsupported operations.
- The host runtime gains an explicit and truthful exception-delivery boundary for future HLE and session orchestration.
- All compatibility manifests continue to state `playable: false` until real-title boot and gameplay acceptance gates are independently satisfied.
