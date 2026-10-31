# ADR 0005: Bounded EE main-RAM load/store slice

- Status: Accepted
- Date: 2026-10-31
- Scope: initial EE byte, halfword and word memory operations

## Context

The scalar AOT slice previously stopped before ordinary memory operations. Real EE code needs byte, halfword and word loads/stores, but treating the whole guest address space as a flat host pointer would be unsafe and would silently turn MMIO into RAM. The PS2's EE memory map includes several main-RAM aliases and distinct hardware/BIOS/VU/IOP/scratchpad ranges. COP0/TLB state can change address translation, so the simplified alias model cannot be described as a complete EE memory system.

## Decision

- Represent LB, LBU, LH, LHU, LW, LWU, SB, SH and SW as typed scalar IR operations with base GPR, 16-bit immediate and source/destination GPR.
- Calculate the effective address using the low 32 bits of the base GPR plus a sign-extended 16-bit offset in 32-bit wrapping arithmetic.
- Add a caller-owned 32 MiB little-endian main-RAM span to EeState; the recompiler runtime does not allocate or own console RAM.
- Resolve the documented main-RAM aliases:
  - 0x00000000..0x01FFFFFF -> physical 0x00000000..0x01FFFFFF.
  - 0x20000000..0x21FFFFFF -> same 32 MiB physical main RAM (uncached alias).
  - 0x30100000..0x31FFFFFF -> physical 0x00100000..0x01FFFFFF (accelerated alias).
  - KSEG0 and KSEG1 addresses translate through the low 29 bits; only addresses resolving into backed main RAM are accepted.
- Assemble/disassemble byte, halfword and word data explicitly in little-endian order.
- Check halfword/word natural alignment before mapping/backing-store access. Misalignment returns address_error.
- Check the caller-provided span before touching memory. A valid guest RAM address without enough backing bytes returns unmapped_memory.
- Recognize hardware/device, reserved, BIOS, IOP, VU, GS and scratchpad regions as unimplemented; return unsupported_memory_access rather than reading/writing main RAM.
- LB/LH/LW sign-extend into GPR[63:0]. LBU/LHU/LWU zero-extend into GPR[63:0]. SB/SH/SW write only the low 8/16/32 source bits. Loads preserve the upper 64-bit GPR lane; a failed load does not alter its destination.
- Perform the memory access even when the load destination is GPR[0], preserving possible future device read side effects.
- Keep MMIO, TLB translation, cache effects, bus errors, alignment exception delivery and COP0 state out of scope for this increment.

## Scope and limitations

This is a main-RAM data path, not a full EE memory subsystem. The implementation assumes the documented default aliases and 32 MiB retail RAM. It does not model expansion RAM, 128 MiB TOOL mappings, COP0/TLB remapping, user/supervisor permissions, cache coherency, scratchpad storage, BIOS reads, IOP RAM, VU memory, GS/IPU/timer/DMA/SIF registers, or exception vectors. All hardware/device/reserved accesses stop explicitly.

## Validation

Synthetic generated code is compiled and executed on Windows/MSVC and Linux/GCC. Tests cover:
- Signed and unsigned byte/halfword/word loads.
- Low-bit store widths and little-endian byte order.
- Negative immediate offsets and 32-bit effective-address arithmetic.
- Main-RAM direct, uncached, accelerated, KSEG0 and KSEG1 aliases.
- Halfword/word alignment faults that preserve the destination and report the faulting guest PC.
- MMIO/reserved versus unmapped address status distinctions.
- A short caller-provided backing span that cannot satisfy an otherwise valid RAM address.
- Preservation of the upper 64-bit lane for load destinations.

No game ISO, BIOS image, or retail asset is committed.
