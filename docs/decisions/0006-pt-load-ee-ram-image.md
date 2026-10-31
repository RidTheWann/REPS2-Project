# ADR 0006: PT_LOAD to default EE main-RAM image

- Status: Accepted
- Date: 2026-10-31
- Scope: initial RAM image for eligible static ELF32 MIPS executables

## Context

The CFG AOT backend can emit native instructions and the runtime has a caller-owned 32 MiB RAM span, but the host still has no deterministic way to create the guest's initial contents from the ELF. Running generated code against a zeroed span is incorrect for any executable that reads initialized global data or expects BSS state.

The ELF parser already validates PT_LOAD file ranges and materializes zero-filled memory tails. The default EE memory map provides direct, uncached, accelerated and KSEG aliases into main RAM. A segment can be valid in virtual space yet collide with another segment after alias translation.

## Decision

- Build a full 32 MiB physical main-RAM snapshot from PT_LOAD segments.
- Support the base, uncached, accelerated, KSEG0 and KSEG1 aliases already implemented by the runtime memory helper.
- Copy file-backed bytes to their translated physical offsets and preserve zero-filled BSS tails.
- Reject segments that leave supported aliases, cross a non-contiguous alias boundary, exceed physical main RAM, or overlap after physical alias resolution.
- Require a static MIPS ET_EXEC image and require the ELF entry point to lie in file-backed executable PT_LOAD data.
- Reject REL/RELA relocations targeting SHF_ALLOC sections until relocation types and addend semantics are implemented and tested. Non-allocated debug relocations do not modify the loaded image.
- Emit an adjacent 32 MiB raw RAM image alongside the generated C++ source.
- Add an entry-specific generated initialize_ee_state wrapper. It checks the size of the RAM snapshot and caller-provided RAM span, copies the image, resets GPR state and sets the ELF entry PC. A failed initialization does not modify guest state or RAM.

## Scope and limitations

This is a default-map static image builder, not a complete PS2 executable loader. It does not process R_MIPS relocations, dynamic linking, ET_DYN, kernel/module metadata, SIF/IOP startup, BIOS services, COP0/TLB mappings, exception state, cache initialization, expansion RAM, custom TOOL memory maps, VU/GS devices, or memory-mapped hardware. Any ELF requiring one of these mechanisms must be rejected or analyzed manually before execution.

## Validation

Synthetic tests verify:
- ET_EXEC MIPS main-RAM image creation.
- PT_LOAD file bytes and zero-filled BSS placement.
- Direct, uncached, accelerated, KSEG0 and KSEG1 alias translation.
- Rejection of non-executable ELF types, unsupported segment addresses and physical alias collisions.
- Rejection of relocations targeting allocated sections.
- Generated initializer copies a full 32 MiB image, resets GPR state and selects the correct entry PC.
- Short image spans are rejected without mutating RAM or guest registers.

No game ISO, BIOS image, proprietary SDK, or retail executable is committed.
