# ADR 0007: EE doubleword load/store operations

- Status: Accepted
- Date: 2026-10-31
- Scope: initial LD/SD support over the bounded EE main-RAM model

## Context

The existing scalar memory path handled byte, halfword and word transfers, while the EE state already represented each GPR as two 64-bit lanes. LQ/SQ handled both lanes as a 128-bit transfer, but the common 64-bit doubleword path was missing. This prevented a useful class of integer code from being translated without an explicit unsupported stop.

The public MIPS64 Architecture for Programmers, Volume I-A, Revision 6.01 identifies LD and SD as CPU doubleword load/store instructions and documents natural-alignment requirements before Release 6. Revision 5.04, Table A.2, assigns primary opcodes 0x37 and 0x3F to LD and SD respectively. PCSX2 1.6.0's R5900 interpreter independently corroborates the EE-specific execution shape used here: 8-byte alignment, access through the low 64-bit GPR lane, and a dummy destination for loads to GPR0. That interpreter is a non-normative implementation cross-check, not an official hardware specification. These references do not resolve the remaining EE virtual-memory, exception, cache, or device model.

## Decision

- Decode primary opcode 0x37 as LD and 0x3F as SD.
- Lift both instructions into explicit typed IR nodes with a base GPR, a target/source GPR and a signed 16-bit displacement.
- Form the effective address using the current EE memory-slice rule: the low 32 bits of the base GPR plus the sign-extended displacement, with 32-bit wrapping.
- Require natural 8-byte alignment. A misaligned access returns the existing `address_error` status before reading/writing memory.
- LD reads exactly eight little-endian bytes into GPR[63:0], preserving GPR[127:64]. A load into GPR[0] still performs and validates the memory access but discards the result.
- SD writes exactly the low 64 bits of the source GPR as eight little-endian bytes; it does not read or write the upper GPR lane.
- Validate the entire access against the resolved main-RAM mapping and caller-owned backing span before modifying the destination register or memory.
- Keep the helpers byte-assembled; do not use unaligned typed pointer casts or host-endian assumptions.

## Non-goals

This increment does not add LDL/LDR/SDL/SDR, LLD/SCD, cache/coherency behavior, TLB translation, exception delivery, MMIO, or memory outside the supported main-RAM aliases. It does not claim compatibility with retail titles.

## Validation

- Decoder tests cover LD/SD opcodes, register fields and negative signed offsets.
- IR/codegen tests verify that the operations lower through checked 64-bit helpers.
- Generated C++ smoke tests execute LD/SD against synthetic RAM through the existing alias matrix, verify exact byte order and width, and check that LD preserves the upper GPR lane.
- Failure tests verify 8-byte alignment, precise destination preservation, and short backing-span rejection for both load and store.
- CI compiles and executes the generated code on Windows/MSVC and Linux/GCC.

No game ISO, BIOS image, proprietary SDK, or retail executable is committed.
