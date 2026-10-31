# ADR 0003: Reachability-guided CFG and explicit delay slots

- Status: Accepted
- Date: 2026-10-31
- Scope: ELF executable analysis and initial control-flow discovery

## Context

The scalar AOT slice only accepts sequential instructions. Real PS2 executables contain branches, jumps, indirect transfers, calls, and architectural delay slots. Treating these as ordinary sequential instructions would corrupt control flow and could cause the linear emitter to generate behavior that does not match the guest.

The ELF loader also materializes PT_LOAD memory tails as zero-filled bytes. Those bytes represent initialized runtime memory (BSS), not file-provided instructions. Static code discovery must not decode zero-filled tails as executable NOPs.

## Decision

- Add a reachability-guided analyzer rooted at the ELF entry point and executable STT_FUNC symbols.
- Read instruction words only from file-backed bytes in executable PT_LOAD segments.
- Represent conditional branches, direct jumps/calls, indirect jumps/calls, returns, unsupported instructions, and segment ends as explicit block terminators.
- Store control instructions and their delay-slot words separately from ordinary block-body instructions.
- Record conditional taken/fallthrough edges, direct jump/call edges, call return continuations, unresolved indirect targets, and external/unmapped targets.
- Split a previously discovered block when a later branch target lands on an instruction inside that block.
- Reject a control-flow target that enters a delay slot; reject unsupported instructions or control transfers inside a delay slot.
- Apply explicit instruction and block limits to the analysis of untrusted input.
- Expose read-only CFG statistics through the analyze-elf CLI command.

## Scope and limitations

The CFG supports only the currently decoded control-transfer subset: J/JAL, BEQ/BNE/BLEZ/BGTZ, BLTZ/BGEZ/BLTZAL/BGEZAL, JR/JALR. It does not yet support branch-likely variants, dynamic target recovery, full function boundary inference, or delay-slot-aware branch lowering to IR/C++.

The analyzer reports syscall/break and other unsupported encodings as block stops. It does not claim full R5900 instruction coverage, a complete executable translation, or game compatibility.

## Validation

Synthetic tests cover:
- Conditional branch taken/fallthrough edges and separate delay-slot representation.
- Backward branches that target the interior of a previously scanned block.
- Rejection of targets entering delay slots.
- Rejection of control transfers inside delay slots.
- Indirect JALR target reporting and return continuation.
- Exclusion of zero-filled BSS tails from executable instruction reads.
- Rejection of inconsistent externally constructed segment metadata.

All fixtures are synthetic and contain no game or BIOS data.
