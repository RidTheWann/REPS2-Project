# ADR 0037: EE SYSCALL/BREAK delay-slot exception delivery

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** CFG delay-slot admission, typed control IR, C++ AOT lowering, and native regression fixtures

## Context

The EE runtime already models Level-1 SYSCALL/BREAK entry and accepts an
in_branch_delay parameter in raise_ee_exception. However, CFG analysis
previously rejected every SYSCALL/BREAK in a delay slot, leaving the runtime's
BD/EPC support reachable only through other exception paths such as signed
arithmetic overflow and checked data-alignment faults.

That gap was observable at the compiler boundary: the decoder knew the trap
opcode, but ControlDelaySlot had no representation for an architectural
exception and the emitter could only lower scalar delay-slot instructions.

## Architectural evidence

Sony's EE Core User's Manual, version 6.0, describes the execution rules in
section 2.5.1: ordinary branches execute the delay slot on both outcomes,
whereas branch-likely instructions nullify it when the branch is not taken.
The same section states that exceptions arising in the delay slot are restartable
from the preceding branch. Sections 4.2.11 and 4.2.12 specify SYSCALL and BREAK
exception handling, including Cause.BD and EPC restart-address behavior.

References:
- Sony, EE Core User's Manual, version 6.0, sections 2.5.1, 4.2.11 and 4.2.12:
  https://docs.alexrp.com/mips/ee.pdf
- PS2Tek, COP0 / exception reference:
  https://psi-rockin.github.io/ps2tek/

## Decision

### 1. Allow only architectural SYSCALL/BREAK traps in delay slots

CFG analysis now admits decoded SYSCALL and BREAK delay-slot instructions.
ERET and other control transfers remain rejected in delay slots. Unsupported
encodings continue to fail explicitly.

### 2. Represent delay-slot traps in the typed control IR

ControlDelaySlot distinguishes:
- Instruction(ScalarInstruction) for the currently lowered scalar subset.
- GuestException { guest_pc, trap } for SYSCALL/BREAK.

This avoids misclassifying a known architectural trap as an unsupported scalar
operation, and lets verification check the delay-slot PC without trying to
scalar-lift a trap.

### 3. Deliver exceptions at the exact point of execution

Generated code writes the delay-slot PC to EeState::pc and calls
raise_ee_exception with in_branch_delay = true. The existing runtime helper
then records EPC as PC - 4, sets Cause.BD, sets EXL when entering Level 1,
and selects the configured common exception vector.

The branch condition and link write are evaluated before the delay slot. A trap
terminates the current generated block before the branch destination is
committed. For branch-likely instructions the trap emission remains inside the
taken-path guard, so an annulled delay slot cannot raise the exception. The
fallthrough path continues normally and can raise its own later exception.

### 4. Preserve unsupported boundaries

This change does not add ERET-in-delay-slot semantics, nested control transfer
support, interrupt delivery, HLE services, or any new BIOS/kernel behavior.
Exception entry is not a claim that the vector handler is translated or that a
real game boots.

## Validation

- Rust CFG tests admit SYSCALL and BREAK while retaining ERET delay-slot
  rejection.
- Control-IR tests preserve trap identity and guest PC in the new delay-slot
  node.
- Code-generation tests assert the exception helper receives the branch-delay
  flag.
- The native generated-C++ fixture covers:
  - taken and not-taken BLTZAL with SYSCALL in its ordinary delay slot,
    including unconditional link-register write semantics;
  - taken BEQL with BREAK in the delay slot;
  - not-taken BEQL annulling BREAK, followed by a non-delay-slot SYSCALL whose
    EPC/BD are independently asserted;
  - JAL with BREAK in the delay slot, proving link and restart-frame ordering.
- The same smoke source is used by both single-unit and split-unit CFG builds.

Synthetic fixtures validate the isolated compiler/runtime semantics only.
They do not establish boot-tested or playable title status.
