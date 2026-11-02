# ADR 0045: MIPS PT_MIPS_REGINFO program-header diagnostics

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** ELF32 EM_MIPS PT_MIPS_REGINFO payload parsing, uniqueness, and ordering

## Context

REPS2 already decoded SHT_MIPS_REGINFO section records, while elf-info only
reported the count of PT_MIPS_REGINFO program headers. The MIPS ABI defines
the program-header form as the runtime-facing register-usage record and says
that it identifies one .reginfo section.

Without parsing the segment payload, section-less or stripped ELF files could
carry register-use metadata that REPS2 counted but did not expose.

## Specification basis

The System V MIPS ABI Supplement defines PT_MIPS_REGINFO as:
- program-header type 0x70000000;
- a unique segment in the file;
- a segment that precedes any PT_LOAD entry;
- a segment identifying one .reginfo type section.

The payload is ELF32_RegInfo, a 24-byte record consisting of one GPR mask,
four coprocessor-register masks, and a signed 32-bit gp value.

References:
- System V MIPS ABI Supplement, §5 Program Header and §4 Register Information:
  https://refspecs.linuxbase.org/elf/mipsabi.pdf
- Linux MIPS ELF definitions for PT_MIPS_REGINFO and ELF_RegInfo:
  https://kernel.googlesource.com/pub/scm/linux/kernel/git/ralf/linux/%2B/4207b28e77fa8770c5037ed73d24399dfbdb9db1/arch/mips/include/asm/elf.h
- GNU ELF definitions for PT_MIPS_REGINFO:
  https://kernel.googlesource.com/pub/scm/utils/kernel/kexec/kexec-tools/%2B/master/include/elf.h

## Decision

### 1. Decode only EM_MIPS objects

A non-MIPS object that uses the processor-specific numeric p_type is left
opaque. This matches the policy used for SHT_MIPS_REGINFO, SHT_MIPS_ABIFLAGS,
and SHT_MIPS_OPTIONS.

### 2. Validate the segment when present

For EM_MIPS:
- at most one PT_MIPS_REGINFO program header is allowed;
- it must precede every PT_LOAD program header;
- p_filesz must be exactly 24 bytes;
- its file range is checked before the payload is read.

The parser exposes the program-header index, file offset, virtual address,
GPR mask, all four coprocessor masks, and signed gp value. The p_memsz value
is not used as a load-size constraint because this metadata segment is not a
PT_LOAD memory image.

### 3. Do not require presence globally

The MIPS ABI supplement describes PT_MIPS_REGINFO as mandatory for its
conforming executable/shared-object environment. REPS2 does not make presence
a universal parsing gate: PS2SDK's EE ERL documentation records a relocatable
module workflow that strips .reginfo, and current research must preserve such
real toolchain variants.

When the segment is absent, the parsed segment list is empty. A corresponding
SHT_MIPS_REGINFO section, if present, is still parsed independently.

### 4. Keep diagnostics separate from loading and compatibility

Register-use masks and gp metadata do not apply GP-relative relocations and do
not establish that the EE runtime supports every register or coprocessor
listed by the producer. PT_MIPS_REGINFO does not change PT_LOAD mapping,
memory initialization, or execution admission.

## Validation

Synthetic in-memory ELF32 fixtures cover:
- a valid 24-byte PT_MIPS_REGINFO payload before PT_LOAD;
- decoding all six ELF32_RegInfo words and preserving signed gp_value;
- duplicate PT_MIPS_REGINFO rejection;
- rejection when PT_MIPS_REGINFO appears after PT_LOAD;
- rejection when p_filesz is not exactly 24 bytes;
- non-EM_MIPS objects retaining the numeric program-header type without
  interpreting a deliberately malformed MIPS payload.

No retail game ELF, ISO, BIOS, or proprietary binary is added.
