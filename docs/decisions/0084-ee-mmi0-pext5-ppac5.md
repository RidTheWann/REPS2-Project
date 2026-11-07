# ADR 0084: EE MMI0 PEXT5 and PPAC5 5-bit color conversion

- Status: Accepted
- Date: 2026-11-07
- Scope: R5900 MMI0 decoder, typed scalar IR, C++ AOT lowering and 128-bit GPR semantics

## Context

The MMI0 data-movement family includes single-source color conversion operations PEXT5 and PPAC5. They are bounded 128-bit register transformations and do not depend on memory, SA, COP0 state, pipeline-1 HI/LO state or control transfer.

## Architectural evidence

The Sony EE Core Instruction Set Manual describes PEXT5 as extending four 1-5-5-5 halfwords in a 128-bit GPR into 8-8-8-8 positions, and PPAC5 as packing/truncating the selected fields back to four 1-5-5-5 halfwords.

Both operations use primary opcode MMI 0x1C and MMI0 selector 0x08. PEXT5 uses bits 10..6 selector 0x1E; PPAC5 uses selector 0x1F. The rs field is reserved-zero; rt is the sole source and rd is the destination.

The four 32-bit lanes are independent. For a word v, PEXT5 produces:

`((v & 0x0000001f) << 3) | ((v & 0x000003e0) << 6) | ((v & 0x00007c00) << 9) | ((v & 0x00008000) << 16)`

PPAC5 extracts the corresponding fields:

`((v >> 3) & 0x0000001f) | ((v >> 6) & 0x000003e0) | ((v >> 9) & 0x00007c00) | ((v >> 16) & 0x00008000)`

Primary reference: Sony EE Core Instruction Set Manual, Version 6.0, PEXT5 entry printed page -202- and PPAC5 entry printed page -254- (public archive: https://github.com/4kbyter/ps2docs/blob/main/EE_Core_Instruction_Set_Manual.pdf). The manual remains outside the repository; only independently written semantics and synthetic test vectors are committed. Cross-check: PCSX2 `pcsx2/MMI.cpp` implements the same four per-word masks/shifts and suppresses the destination-zero write.

## Decision

1. Decode only primary 0x1C, MMI0 selector 0x08, and the exact PEXT5/PPAC5 field selectors. Reject encodings with nonzero rs. Preserve legal sibling decoding: selectors 0x1E/0x1F under MMI2 (function 0x09) are PEXEW/PROT3W, not illegal PEXT5/PPAC5 words.
2. Add typed single-source IR operations. Effects read rt and write rd only; no memory, SA, exception, or control-flow dependency is introduced.
3. Compute a complete Gpr128 result before assigning it to rd. Each 32-bit word is transformed independently. A destination aliasing rt reads the original 128-bit source, and writes to GPR[0] remain suppressed.
4. PPAC5 is truncating field extraction, not rounding or saturation. PEXT5 preserves the source bit positions rather than normalizing five-bit channels to the full eight-bit range.

## Validation

- Decoder unit tests cover exact opcodes (including round-trip word `0x70033FC8`), mnemonics, MMI0 family, complete effects, all 31 illegal nonzero rs values, an unsupported neighboring selector, and legal MMI2 siblings.
- IR and codegen unit tests prove distinct typed operations, single-source operand use, GPR[0] suppression, and helper selection.
- A generated-C++ CTest fixture checks hand-derived values for all four lanes, rd==rt aliasing for both operations, poisoned entry GPR0 normalization, zero sources/destinations, untouched registers and HI/LO/HI1/LO1/SA, and a PEXT5-to-PPAC5 round trip. An independent concatenation-based oracle checks every input bit of both helpers, including discarded bits.
- Single- and split-unit CFG native fixtures execute PEXT5 in a block body and PPAC5 in taken/annulled branch-likely and JR delay slots. These tests exercise the shared lowering path and C++ helper lookup in both output layouts.

## Consequences

PEXT5/PPAC5 join the lowered MMI0 subset. This is not full MMI0–MMI3 coverage and is not retail-title boot evidence. Remaining MMI operations, pipeline-1 timing/state, and the wider EE/VU/IOP runtime remain explicit work.
