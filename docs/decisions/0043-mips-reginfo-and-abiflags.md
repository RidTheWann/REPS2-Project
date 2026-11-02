# ADR 0043: MIPS .reginfo and .MIPS.abiflags metadata diagnostics

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** ELF32 EM_MIPS section metadata for register usage and ABI flags

## Context

REPS2 already decoded MIPS e_flags, including the R5900 CPU subtype code, but
did not expose the processor-specific section metadata commonly emitted by
MIPS toolchains. Two section types carry useful additional information:

- SHT_MIPS_REGINFO describes GPR/coprocessor register usage and the global
  pointer value;
- SHT_MIPS_ABIFLAGS describes the ISA/register-size/FP-ABI profile and
  processor-specific ISA extension/ASE masks.

For PS2 work, a modern R5900 toolchain can identify the EE extension in
.MIPS.abiflags separately from the older e_flags CPU subtype field.

## Specification and toolchain evidence

The MIPS ABI Supplement defines ELF32_RegInfo as six 32-bit words:
ri_gprmask, ri_cprmask[4], and signed ri_gp_value. It defines
PT_MIPS_REGINFO as the related program-header type and documents its
ordering/uniqueness constraints.

The version-zero MIPS ABI flags record is 24 bytes and contains a 16-bit
version followed by the ISA level/revision, GPR/CPR1/CPR2 register-size
codes, FP ABI, ISA extension, ASE mask, and two general flag words.

LLVM's R5900 target support uses AFL_EXT_5900 = 6 in the ISA extension field.
This is distinct from the e_flags CPU subtype value 0x92.

References:
- System V MIPS ABI Supplement, §§4.14 and 5.4:
  https://refspecs.linuxfoundation.org/elf/mipsabi.pdf
- Linux MIPS ELF definitions:
  https://android.googlesource.com/kernel/common/+/b83b3fa78445387f351cef477a112e503d72b9f0/arch/mips/include/asm/elf.h
- LLVM MIPS ABI flags definitions:
  https://www.llvm.org/docs/doxygen/MipsABIFlags_8h.html
- LLVM R5900 target support and AFL_EXT_5900:
  https://lists.llvm.org/pipermail/llvm-commits/Week-of-Mon-20260119/1826509.html
- PS2SDK EE ERL documentation:
  https://github.com/ps2dev/ps2sdk/blob/master/ee/erl/README

## Decision

### 1. Decode SHT_MIPS_REGINFO for EM_MIPS only

Each SHT_MIPS_REGINFO must contain exactly one 24-byte ELF32_RegInfo record.
The parser preserves:
- section index;
- ri_gprmask;
- all four ri_cprmask entries;
- ri_gp_value as a signed 32-bit value.

No claim is made that a register mask proves which instructions a game uses;
it is metadata supplied by the object producer.

### 2. Decode SHT_MIPS_ABIFLAGS version zero

The parser reads the structure version first. Version zero must be exactly
24 bytes and is decoded into its documented fields. Register-size and FP ABI
codes receive generic labels; the raw numeric fields remain available.

AFL_EXT_5900 (value 6) is identified as the R5900 extension. ASE names are
reported for recognized mask bits; flags1, flags2, unknown ASE bits and
unknown extension values remain raw.

Unknown nonzero structure versions are preserved as the section index, raw
version and byte size, and are not decoded with the version-zero layout.

### 3. Keep format recognition separate from admission policy

The metadata is interpreted only for EM_MIPS files. An absent .reginfo section
is not a parse error. The MIPS ABI supplement specifies PT_MIPS_REGINFO
uniqueness/ordering for its executable/shared-object ABI, but REPS2 reports
the number of such headers rather than enforcing that requirement globally.

This is deliberate: PS2SDK's EE ERL documentation shows a relocatable-module
workflow where .reginfo is removed. REPS2 is still cataloguing real toolchain
and title variants and must not turn this generic ABI diagnostic into an
unverified universal PS2 acceptance gate.

The parser does not apply GP-relative relocations, perform dynamic linking,
or claim retail-title compatibility from these fields.

## Validation

Synthetic in-memory ELF32 fixtures cover:
- all six ELF32_RegInfo words and signed gp_value;
- version-zero MIPS ABI flags decoding;
- AFL_EXT_5900 = 6;
- recognized ASE mask names and preservation of flags1/flags2;
- unknown ABI flags version retention without version-zero misinterpretation;
- malformed .reginfo size and malformed version-zero ABI flags size;
- skipping processor-specific MIPS interpretations for non-EM_MIPS machines.

No proprietary ELF, game ISO, BIOS, or third-party source file is committed.
