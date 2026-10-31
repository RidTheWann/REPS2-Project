# ADR 0017: R5900 branch-likely annulment and unconditional link writes

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** Rust decoder, CFG analysis, typed control IR, C++20 AOT backend, and native smoke tests for the branch-likely family

## Context

The initial CFG backend supported only non-likely conditional branches. R5900 code uses branch-likely forms to avoid executing a delay-slot instruction when the branch is not taken. Treating these encodings as unknown stops translation. Treating them as ordinary branches would execute the delay slot on the fallthrough path and produce guest-state transitions that differ from hardware.

The EE Core Instruction Set Manual and MIPS32 Volume II define the same split behavior for the branch-likely family: evaluate the branch predicate, execute the delay-slot instruction only when the branch is taken, and continue at PC+8 with the delay slot annulled when it is not taken. BLTZALL and BGEZALL additionally write the PC+8 link address to GPR[31] independently of the predicate. PCSX2's R5900 opcode tables and dynamic recompiler are used only as an independent behavioral cross-check for the encodings and link ordering.

## Decision

Recognize the R5900 branch-likely encodings and carry annulment as explicit data through analysis, IR, and code generation.

### Decoder

- Primary opcode `0x14` is BEQL and `0x15` is BNEL.
- Primary opcode `0x16` is BLEZL and `0x17` is BGTZL, both requiring `rt == 0`. Other `rt` values remain unsupported.
- REGIMM (`0x01`) `rt` values `0x02`, `0x03`, `0x12`, and `0x13` are BLTZL, BGEZL, BLTZALL, and BGEZALL.
- Every recognized form has a delay slot, computes its target as `PC + 4 + sign_extend(offset) << 2`, and is classified as a conditional direct branch or conditional direct call.
- BLTZALL and BGEZALL expose GPR[31] as an unconditional write in the instruction-effect metadata.

### Analysis and IR

`DelaySlotPolicy` is a typed enum with two values:

- `ExecuteAlways`: the ordinary delayed-branch behavior.
- `AnnulWhenNotTaken`: the branch-likely behavior.

Conditional-branch terminators carry the policy alongside the target, fallthrough PC, link register, and explicit delay-slot instruction. The typed control IR preserves the policy instead of encoding it as a string or a special opcode check in the backend.

### Code generation

The generated block evaluates and stores the branch predicate first. It then writes any link address, guards the delay-slot code with the predicate when the policy is `AnnulWhenNotTaken`, and finally commits either the branch target or PC+8. The guard is applied to the delay-slot side effects only; the link write is outside it, so BLTZALL/BGEZALL write PC+8 on both paths.

This ordering matters when the delay-slot instruction writes the link register or when the link write must remain visible after annulment. Tests assert the emitted statement order rather than only final register values.

## Validation

- Decoder tests cover all eight encodings, reserved `rt` rejection for BLEZL/BGTZL, target calculation, delay-slot metadata, and branch-likely classification.
- Effect tests cover predicate reads and unconditional GPR[31] writes for BLTZALL/BGEZALL.
- CFG and typed-IR tests cover the annulment policy and link-register propagation.
- Code-generation tests assert condition, link-write, guard, and dispatch ordering.
- Native C++ smoke cases execute taken and not-taken paths for BEQL and BLTZALL, including delay-slot side-effect absence on the fallthrough path and the unconditional link value. They pass locally on Windows/MSVC.
- Linux/GCC validation remains the responsibility of the CI matrix after the change is pushed; local success is not presented as cross-platform evidence.

## Boundaries

This increment does not claim complete R5900 control-transfer coverage. Branch targets that enter another instruction's delay slot, control transfers or unsupported encodings inside a delay slot, branch-delay exception state, and exception delivery remain unsupported and must stop explicitly. It also does not add differential execution against hardware or a full instruction-set oracle.

## References

- Sony Computer Entertainment, *EE Core Instruction Set Manual*, Version 6.0 (2002): https://docs.alexrp.com/mips/ee_insns.pdf
- MIPS Technologies, *MIPS32 Architecture for Programmers, Volume II: The MIPS32 Instruction Set*.
- PCSX2 R5900 opcode tables and branch recompiler, used only as an independent behavioral cross-check; no source is copied.
- REPS2 [research notes](../RESEARCH.md), [decoder](../../rust/frontend/src/mips.rs), and [CFG analysis](../../rust/frontend/src/analysis.rs).
