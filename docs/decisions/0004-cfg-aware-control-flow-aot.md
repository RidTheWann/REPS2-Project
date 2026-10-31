# ADR 0004: CFG-aware typed IR and bounded C++ dispatcher

- Status: Accepted
- Date: 2026-10-31
- Scope: first executable control-flow AOT lowering

## Context

The first C++ AOT emitter accepts a verified straight-line scalar IR. The preceding CFG milestone can discover a bounded set of branches and jumps, but the graph is not useful to a compiler unless control-flow and delay-slot semantics survive the lowering boundary.

Flattening a branch into a linear instruction list is incorrect. In particular, branch predicates are evaluated from the state at the branch instruction, the delay-slot instruction executes before the selected target PC is committed, branch-and-link writes PC+8 even for a not-taken BLTZAL/BGEZAL, and JALR must capture its source target before writing a potentially aliased link register.

## Decision

- Preserve the scalar IR as the typed representation for ordinary instructions.
- Add a CFG-aware ControlProgram containing typed basic blocks and explicit control-flow terminators.
- Keep each delay-slot operation separate from the branch/jump terminator.
- Verify each block's instruction sequence, terminator PC, delay-slot PC+4, entry points, and register operands.
- Lower the supported graph to a deterministic C++20 PC dispatcher over EeState::pc.
- Evaluate branch conditions before link-register writes and delay-slot execution.
- Write link register PC+8 independently of the BLTZAL/BGEZAL predicate.
- Capture JR/JALR dynamic target values before the delay slot and before JALR link writes.
- Execute the delay-slot operation before assigning the selected next PC.
- Bound execution with a guest-instruction budget. Blocks are admitted atomically; if the remaining budget cannot cover a whole block, the dispatcher returns without partially executing that block.
- Return structured runtime statuses for instruction-budget exhaustion, unsupported instructions, and control flow leaving the translated graph.
- Add an ELF-to-CFG-IR-to-C++ command for the currently implemented subset and a synthetic control-flow fixture compiled/executed by CTest.

## Scope and limitations

This is not a full R5900 backend. Branch-likely/annul instructions, exception and branch-delay exception state, load/store and memory semantics, self-modifying code, COP0/FPU/MMI, dynamic target recovery, and PS2 hardware services remain unsupported. A control transfer whose destination is outside the generated block map returns external_control_flow with EeState::pc set to the destination so a future runtime dispatcher can decide the next action. Unsupported guest instructions stop explicitly; they are not approximated.

The guest-instruction budget is charged per complete basic block. This gives a simple bounded AOT contract but is not yet a cycle-accurate scheduler and does not permit pre-emption in the middle of a block.

## Validation

Synthetic generated-code tests execute:
- Conditional branch taken and not taken paths.
- Delay-slot effects on each path.
- Direct J and JAL control transfers.
- JAL return-address write and JR RA return continuation.
- Explicit unsupported-instruction stop.
- Instruction-budget exhaustion without partial block execution.

Additional Rust tests verify typed IR construction, unsupported instruction preservation, branch condition ordering, unconditional branch-link writes, and JALR target capture before an aliased link write.

No game or BIOS image is included. No playable-title or compatibility claim is made.
