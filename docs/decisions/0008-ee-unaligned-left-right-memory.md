# ADR 0008: EE left/right partial memory operations

- Status: Accepted
- Date: 2026-10-31
- Scope: LWL/LWR/SWL/SWR and LDL/LDR/SDL/SDR over the bounded EE main-RAM model

## Context

The initial EE memory slice supported byte, halfword, word, doubleword and quadword transfers, but did not include the left/right operations used to assemble unaligned words and doublewords. These instructions are not ordinary unaligned host loads: the low address bits select which byte lanes are merged with the old register or memory value.

MIPS64 architecture references define the family of left/right partial transfers. The PCSX2 R5900 interpreter is used only as a non-normative cross-check for the EE-specific masks, shifts and 128-bit GPR lane effects. The independent REPS2 implementation uses checked aligned accessors and synthetic tests; no emulator source is copied.

## Decision

- Decode the primary opcodes LWL=0x22, LWR=0x26, SWL=0x2A, SWR=0x2E, LDL=0x1A, LDR=0x1B, SDL=0x2C and SDR=0x2D.
- Preserve the signed 16-bit displacement and calculate the effective address using the existing low-32-bit base plus sign-extended immediate rule with 32-bit wrapping.
- For the word family, derive the byte offset from address bits 1:0 and read/write the containing aligned 4-byte word. For the doubleword family, derive the byte offset from bits 2:0 and read/write the containing aligned 8-byte unit.
- LWL merges the selected high-order memory bytes into the low 32-bit register value and sign-extends the merged 32-bit result into GPR[63:0]. LWR merges the complementary low-order memory bytes; at byte offset zero it sign-extends the full word, and at other offsets it preserves GPR[63:32] while replacing GPR[31:0].
- LDL/LDR merge the selected bytes into GPR[63:0]. All four partial load operations preserve GPR[127:64]. A load to GPR0 still performs and validates its aligned memory read but discards the value.
- SWL/SWR merge the selected low-32-bit source bytes into an existing aligned memory word. SDL/SDR do the same for the low 64-bit source lane and an aligned doubleword.
- Partial stores first read the complete old aligned unit, form the merged value, and then use the existing checked store helper. No unaligned pointer dereferences, host-endian assumptions, or unchecked buffer access are permitted.
- Report existing unmapped/device statuses unchanged. Validate the complete aligned unit before mutating a destination register or memory. These opcodes do not request an unaligned host access and therefore do not use the ordinary LW/LD alignment-fault path.

## Non-goals

This increment does not add COP0/TLB translation, cache coherency, memory barriers, exception delivery, MMIO devices, LL/SC, cross-device bus behaviour, or any claim of retail-game compatibility. It does not implement a general virtual-memory layer.

## Validation

- Decoder tests identify all eight opcodes and retain signed immediate decoding.
- Typed IR lifting and scalar/CFG code-generation tests cover all eight operations.
- Generated C++ fixtures exercise every byte offset for the word and doubleword families.
- Tests verify exact merge results, little-endian memory layout, preservation of unaffected GPR lanes, and failure atomicity when the caller-owned backing span is too short.
- Windows/MSVC and Linux/GCC CI compile and execute the generated fixtures.

No game ISO, BIOS image, proprietary SDK, retail executable, or copied emulator implementation is committed.
