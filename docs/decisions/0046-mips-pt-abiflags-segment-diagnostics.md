# ADR 0046: MIPS PT_MIPS_ABIFLAGS program-header diagnostics

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** ELF32 EM_MIPS PT_MIPS_ABIFLAGS payload parsing and shared ABI-flags decoding

## Context

REPS2 already decoded SHT_MIPS_ABIFLAGS section payloads but did not inspect
the PT_MIPS_ABIFLAGS program-header representation. MIPS toolchains can map
the same ABI-flags record to a dedicated segment, separately from
PT_MIPS_REGINFO.

The Linux MIPS VDSO linker script explicitly assigns the ABI flags section to
a PT_MIPS_ABIFLAGS header and places that header before PT_MIPS_REGINFO and
PT_LOAD. This is direct evidence that the segment form is used by real MIPS
toolchains, even though the original MIPS ABI supplement predates it.

## Specification and toolchain basis

- Linux MIPS ELF headers define PT_MIPS_ABIFLAGS as 0x70000003:
  https://kernel.googlesource.com/pub/scm/linux/kernel/git/ralf/linux/%2B/4207b28e77fa8770c5037ed73d24399dfbdb9db1/arch/mips/include/asm/elf.h
- Linux MIPS VDSO linker script assigns the .mips_abiflags output section
  to a dedicated abiflags 0x70000003 program header, before the reginfo header:
  https://code.googlesource.com/linux/torvalds/linux/%2B/75f2c0b3690702c90863c2e138cb5520670845ea/arch/mips/vdso/vdso.lds.S
- LLVM ELF definitions name PT_MIPS_REGINFO, PT_MIPS_RTPROC,
  PT_MIPS_OPTIONS and PT_MIPS_ABIFLAGS:
  https://llvm.googlesource.com/llvm-project/%2B/076aac59acbe7555b922e77886e4428f1aa1cd0b/llvm/include/llvm/BinaryFormat/ELF.h

## Decision

### 1. Decode only EM_MIPS objects

A non-MIPS object using the processor-specific numeric p_type is left
opaque. This follows the existing policy for MIPS-specific sections and
PT_MIPS_REGINFO.

### 2. Share one payload decoder between section and segment forms

The SHT_MIPS_ABIFLAGS and PT_MIPS_ABIFLAGS forms use the same versioned
payload decoder. Version zero must be exactly 24 bytes and exposes ISA
level/revision, register sizes, FP ABI, ISA extension, ASE mask, and flags1/
flags2. Unknown versions retain the numeric version and byte size but are
never interpreted using the version-zero layout.

### 3. Validate the segment without treating it as a loadable image

For EM_MIPS:
- at most one PT_MIPS_ABIFLAGS program header is allowed;
- its file range is bounds-checked before decoding;
- version zero is exactly 24 bytes;
- unknown versions need at least the two-byte version field and are retained
  opaquely.

The parsed segment stores its program-header index, file offset, virtual
address, payload size, raw version, and optional version-zero interpretation.
This metadata does not alter PT_LOAD placement, memory initialization, or
execution admission. No universal segment-order rule is imposed by this
diagnostic parser; the Linux VDSO linker script is an observed producer
layout, not a generic MIPS rejection rule.

### 4. Keep segment presence and compatibility separate

The parser does not require a PT_MIPS_ABIFLAGS segment to exist. The section
form remains independently parsed when present. A version label or R5900
extension value is not proof that every instruction or runtime service is
implemented, and no title compatibility claim follows from the metadata.

## Validation

Synthetic ELF32 fixtures cover:
- a valid version-zero PT_MIPS_ABIFLAGS payload and the R5900 extension code;
- shared version-zero field decoding for program-header data;
- preservation of an unknown future version without v0 interpretation;
- rejection of malformed/truncated v0 payloads;
- rejection of duplicate PT_MIPS_ABIFLAGS headers;
- non-EM_MIPS files retaining the numeric program-header type without
  interpreting its payload.

No retail ISO, ELF, BIOS, or proprietary binary is included.
