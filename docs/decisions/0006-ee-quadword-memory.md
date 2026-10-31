# ADR 0006: EE quadword memory operations

- Status: Accepted
- Date: 2026-10-31
- Scope: initial EE LQ/SQ support over the bounded main-RAM model

## Context

The EE general-purpose register file is 128 bits wide. The existing memory IR handled byte, halfword and word operations but could not preserve or transfer the full GPR value to memory. LQ and SQ are core integer load/store instructions for 128-bit values. Their EE-specific behavior silently rounds the effective address down to a 16-byte boundary instead of raising a natural-alignment address error.

## Decision

- Decode primary opcodes 0x1E (LQ) and 0x1F (SQ) as explicit operations.
- Represent them as typed `LoadQuadword` and `StoreQuadword` IR nodes with base GPR, signed 16-bit displacement and source/destination GPR.
- Compute the 32-bit wrapping effective address from the low 32 bits of the base GPR and sign-extended displacement.
- Round the effective address down using `address & ~0x0F` before the 16-byte main-RAM bounds check.
- Transfer the low and high 64-bit GPR lanes in little-endian order using explicit byte shifts/assembly. Do not reinterpret host pointers or assume host alignment.
- LQ replaces both lanes of its destination GPR; GPR[0] remains hard-wired to zero. SQ writes all 16 source bytes.
- Preserve the existing explicit statuses for unsupported devices, unmapped memory and insufficient host backing spans.

## Limitations

This does not implement LDL/LDR, SDL/SDR, LWL/LWR, SWL/SWR, cache/coherency effects, TLB/COP0 translation, or MMIO. LQ/SQ to non-main-RAM regions remain unsupported. It does not implement LQC2/SQC2, which target VU0 vector registers and belong to the COP2/VU0 workstream.

## Validation

- Decoder and typed IR tests cover both opcodes and signed displacements.
- Generated C++ is compiled and executed on Windows/MSVC and Linux/GCC.
- Runtime fixture checks both 64-bit lanes, byte ordering, silent 16-byte rounding from an unaligned effective address, SQ round-trip, and short backing-span rejection.
- No game ISO, BIOS image or retail binary is committed.
