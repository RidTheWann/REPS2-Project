# ADR 0013: conservative R5900 instruction-effect metadata

- **Status:** Accepted
- **Date:** 2026-10-31
- **Scope:** Rust decoder metadata only; no new instruction is claimed as decoded or executable

## Context

The decoder already recognizes a bounded set of EE integer, control-flow, and memory operations. Static analysis and later optimizer passes need more than a mnemonic and encoded register fields: they need explicit data dependencies, special-register effects, memory behavior, control-flow classification, and a conservative indication of synchronous exceptions.

Without a shared effect description, every consumer would have to reconstruct operand roles independently. That duplicates opcode knowledge and risks incorrect liveness, dead-code elimination, call analysis, or memory reordering. It is especially unsafe to interpret an unsupported EE/COP/FPU/MMI encoding as a side-effect-free instruction.

The EE Core Instruction Set Manual describes instruction formats, execution behavior, restrictions, and exception conditions as separate parts of each instruction definition. The MIPS instruction manuals likewise distinguish operation-caused exceptions from instruction-fetch faults and asynchronous interrupts. REPS2 therefore needs an effect summary whose scope and uncertainty are explicit.

## Decision

Add `Instruction::effects()`, returning a typed `InstructionEffects` value for the opcodes currently recognized by the decoder.

The summary records:

- **GPR read/write masks:** bit N represents architectural GPR N. For known instructions, GPR[0] is omitted because reads return the constant zero and writes are discarded.
- **HI/LO dependencies:** separate read/write flags for the base HI and LO registers. This does not imply that the unimplemented HI1/LO1 or MMI accumulator state is modeled.
- **Memory effect:** none, read, write, or unknown. Partial left/right loads consume the prior destination GPR; partial stores remain guest-visible writes.
- **Control flow:** none, conditional direct branch, direct jump/call, indirect jump/call, synchronous trap, or unknown.
- **Possible synchronous exception:** includes known signed integer overflow operations, explicit SYSCALL/BREAK traps, and memory operations that may fail due to address/alignment/translation or backing behavior. Instruction-fetch faults and asynchronous interrupts are out of scope.

The summary is a conservative may-effect description, not a replacement for instruction semantics. It does not implement exception delivery, timing, pipeline behavior, or instruction execution.

## Unknown encodings

An `Unknown` instruction reports all non-zero GPRs as potentially read and written, both HI/LO registers as potentially read and written, unknown memory and control-flow effects, and a possible exception. This deliberately blocks consumers from assuming that an unsupported EE extension, coprocessor instruction, or malformed encoding has no observable side effects.

Unknown instructions must remain explicit unsupported diagnostics in lifting/code generation. The effect summary does not make them executable.

## Architectural details encoded

- `ADD`, `ADDI`, and `SUB` are marked as potentially raising integer-overflow exceptions; their non-trapping unsigned counterparts are not.
- `JAL`, `JALR`, `BLTZAL`, and `BGEZAL` expose their encoded link-register destination. The conditional link forms remain conditional control-flow operations; their link write is represented independently from the branch outcome.
- `MFHI`/`MFLO` consume the corresponding special register; `MULT`/`MULTU`/`DIV`/`DIVU` produce both HI and LO.
- Loads and stores are marked as potentially faulting memory operations. Left/right merge loads read the old destination value as well as the base register.
- Known GPR dependencies omit register zero, while memory and exception effects remain present even when a load's encoded destination is GPR zero.

These rules apply only to encodings already accepted by the decoder. They are not a statement of complete R5900 coverage.

## Validation

Synthetic decoder tests cover:

- scalar source/destination masks and hard-wired GPR zero;
- HI/LO producer and consumer dependencies;
- left/right partial load/store dependencies;
- direct/indirect calls and conditional link branches;
- signed-overflow, synchronous-trap, and unknown-instruction behavior.

The existing Windows and Linux CI matrix remains the authoritative compiler/test validation. Passing these tests validates metadata for the covered decoder subset only; it does not establish retail-game compatibility.

## References

- Sony Computer Entertainment, *EE Core Instruction Set Manual*, Version 6.0 (2002), CPU and EE-specific instruction descriptions: https://docs.alexrp.com/mips/ee_insns.pdf
- MIPS Technologies, *MIPS32 Architecture for Programmers, Volume II: The MIPS32 Instruction Set*, Revision 0.95, instruction Operation and Exceptions field conventions: https://courses.cs.washington.edu/courses/cse378/documentation/MIPS32_Architecture_For_Programmers_Volume_II.pdf
- REPS2 [research notes](../RESEARCH.md) and [decoder](../../rust/frontend/src/mips.rs).
