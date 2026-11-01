# ADR 0034: EE signed-overflow exception delivery and general-vector correction

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** R5900 signed word/doubleword ALU lowering, EE COP0 exception state, branch-delay handling, generated C++ and native regression fixtures
- **Supersedes:** ADR 0033's normal common-vector address and its statement that delay-slot exception delivery was only helper-tested

## Context

The scalar backend already detected signed overflow for the trapping R5900 operations, but converted it to the host-facing `Status::integer_overflow`. That stopped translated execution without creating the guest COP0 exception frame. This was incomplete for guest code: the EE must observe Cause.ExcCode 12, EPC, Status.EXL and the selected general exception vector. No faulting destination register may be committed.

A review of ADR 0033 also found that the normal BEV=0 vector constant was incorrect. It used `0x80000200`, which the Sony EE manual assigns to the interrupt vector, not the common vector used by synchronous exceptions. Consequently, the already-implemented SYSCALL and BREAK path was routed to the wrong vector.

## Architectural evidence

Sony's *Emotion Engine Core User's Manual*, Version 6.0, is the primary reference:

- Table 4-1 distinguishes `V_COMMON` from `V_INTERRUPT`. `V_COMMON` is `0x80000180` when BEV=0 and `0xBFC00380` when BEV=1. `V_INTERRUPT` is `0x80000200` when BEV=0 and `0xBFC00400` when BEV=1.
- Table 4-3 maps SYSCALL (ExcCode 8), BREAK (ExcCode 9), and OVERFLOW (ExcCode 12) to `V_COMMON`.
- The instruction descriptions specify overflow exceptions for ADD, ADDI, SUB, DADD, DADDI and DSUB. The unsigned counterparts are modulo operations and do not raise this exception.

Independent implementation cross-checks include the PS2Tek COP0/exception reference and PCSX2's R5900 interpreter/exception implementation.

References:

- Sony, *Emotion Engine Core User's Manual*, Version 6.0, Chapter 4 and the ADD/ADDI/SUB/DADD/DADDI/DSUB entries: https://docs.alexrp.com/mips/ee.pdf
- PS2Tek, COP0 and exception reference: https://psi-rockin.github.io/ps2tek/
- PCSX2, R5900 opcode implementation: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeImpl.cpp
- PCSX2, R5900 exception/vector implementation: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900.cpp

## Decision

### 1. Correct the vector map

The runtime defines four distinct named addresses:

| Vector | BEV=0 | BEV=1 |
|---|---:|---:|
| Common synchronous exception | `0x80000180` | `0xBFC00380` |
| Interrupt | `0x80000200` | `0xBFC00400` |

`raise_ee_exception` always selects the common synchronous vector. Interrupt delivery remains a separate, unimplemented subsystem; the interrupt address must never be reused as the synchronous exception target.

This corrects ADR 0033's `kEeCommonExceptionVector` value. SYSCALL and BREAK therefore receive the same routing correction, not just newly added overflow exceptions.

### 2. Deliver the supported signed-overflow exceptions

Add `ExceptionCode::Overflow = 12`. The scalar and CFG code generators deliver architectural exception entry for:

- 32-bit word operations: ADD, ADDI and SUB.
- 64-bit doubleword operations: DADD, DADDI and DSUB.

The unsigned operations ADDU, ADDIU, SUBU, DADDU, DADDIU and DSUBU remain modulo arithmetic and never use the overflow exception path.

The generated C++ continues to calculate result bits using unsigned fixed-width arithmetic or a widened signed 32-bit intermediate. It does not evaluate an overflowing signed C++ expression. On overflow it returns `raise_ee_exception(state, ExceptionCode::Overflow, in_branch_delay)` before writing the destination. The helper sets Cause.ExcCode=12, records EPC/BD when EXL was clear, sets EXL, chooses V_COMMON, and returns `Status::exception_delivered`. The existing nested-exception rule remains: while EXL is already set, update ExcCode but preserve the original EPC and BD frame.

Overflow is still detected when the encoded destination is GPR[0]. The destination write is suppressed, but the architectural exception is not.

### 3. Preserve branch-delay semantics

The scalar emitter receives an explicit `in_branch_delay` context:

- Ordinary instructions set the current guest PC and use `false`.
- A control-flow terminator sets the delay-slot PC and uses `true`.
- On overflow in a delay slot, the helper computes EPC=PC-4 and sets Cause.BD before transferring to V_COMMON. The generated early return prevents the branch's post-delay target assignment from overwriting the exception vector.
- Branch-likely lowering already guards the delay slot with the branch predicate. A not-taken branch-likely therefore does not execute the faulting instruction and cannot raise its overflow exception.

SYSCALL/BREAK instructions in delay slots remain rejected by CFG lifting; this decision does not silently broaden those unsupported control-flow cases.

### 4. Preserve legacy host status numbering

`Status::integer_overflow` remains in the public runtime enum for compatibility with previously generated objects or downstream code. The current frontend no longer emits it for the six supported trapping signed arithmetic instructions; they return `Status::exception_delivered` after populating COP0 state.

### 5. Keep the host-service boundary explicit

`Status::exception_delivered` reports a completed guest exception-state transition. It does not mean that the exception handler ran or that a BIOS/kernel service was emulated. The dispatcher/host may resume only when it has a translated handler for the resulting PC. No fake service success is introduced.

## Validation

The regression set covers:

- Normal and BEV bootstrap V_COMMON selection, plus literal assertions that the interrupt vectors remain distinct.
- SYSCALL/BREAK use of the corrected common vector.
- ADD/ADDI/SUB and DADD/DADDI/DSUB overflow with Cause.ExcCode 12, exact guest EPC, EXL, clear BD for ordinary instructions, and no destination write.
- Overflow with GPR[0] as the encoded destination.
- Overflow from an executed branch delay slot, including EPC pointing at the owning branch, BD set, and branch-link side effects preserved.
- A not-taken branch-likely whose delay slot would overflow, proving that the slot is annulled and the following guest instruction is reached instead.
- Existing nested-exception frame preservation and ERET behavior.
- Native generated-code compilation and execution under Windows/MSVC and Linux/GCC CI, plus Rust formatting, Clippy and unit tests.

Fixtures are synthetic and establish only these isolated semantics. They do not establish retail-title compatibility or playability.

## Consequences and remaining limits

- The synchronous exception vector is now architecturally correct for every exception cause currently delivered.
- The supported signed integer arithmetic behaves like a guest EE exception rather than a terminal host diagnostic.
- Delay-slot overflow is precise without introducing a new control-flow terminator or weakening branch-likely annulment.
- General interrupt delivery, other synchronous exception causes, TLB refill, bus errors, Level-2 events, BIOS/HLE services and complete COP0 semantics remain out of scope. The overall PS2 runtime and REPS2 multi-game compatibility gates remain incomplete.
