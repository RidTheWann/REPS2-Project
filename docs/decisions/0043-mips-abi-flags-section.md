# ADR 0043: MIPS SHT_MIPS_ABIFLAGS section diagnostics

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** ELF32 MIPS section type SHT_MIPS_ABIFLAGS, version-zero decoding, and CLI diagnostics

## Context

The ELF frontend already decoded generic MIPS e_flags and reported the R5900
CPU subtype. However, modern MIPS ELF objects may also contain a
SHT_MIPS_ABIFLAGS section. This section supplies complementary metadata about
the ISA level, register widths, FP ABI, processor extension, ASE usage and
general flags.

It is useful to expose these fields for research, but they are not an ABI
validation rule and must not imply a title is playable.

## Specification and toolchain evidence

SHT_MIPS_ABIFLAGS has section type 0x7000002A. The version-zero payload is a
24-byte structure containing:
- a little-endian 16-bit version;
- ISA level and revision;
- GPR, CPR1 and CPR2 register-size codes;
- FP ABI code;
- 32-bit ISA extension, ASE, flags1 and flags2 words.

The register-size values are 0 (none), 1 (32-bit), 2 (64-bit) and 3
(128-bit). The FP ABI codes 0 through 7 distinguish any, double, single,
soft-float, old-64, FPXX, FP64 and FP64A. LLVM defines AFL_EXT_5900 as
extension value 6 for the R5900 instruction set.

References:
- Linux MIPS ELF header and ABI flags structure:
  https://kernel.googlesource.com/pub/scm/linux/kernel/git/ralf/linux/%2B/4207b28e77fa8770c5037ed73d24399dfbdb9db1/arch/mips/include/asm/elf.h
- LLVM MIPS ABI flag constants, including AFL_EXT_5900:
  https://www.llvm.org/docs/doxygen/MipsABIFlags_8h_source.html
- Binutils readelf checks the ABI-flags payload against the v0 structure size:
  https://chromium.googlesource.com/native_client/nacl-binutils/%2B/upstream/master/binutils/readelf.c

## Decision

### 1. Parse only MIPS objects

The section is interpreted only when e_machine is EM_MIPS. A non-MIPS object
that happens to contain this processor-specific section type is left opaque.

### 2. Decode version zero conservatively

Exactly one SHT_MIPS_ABIFLAGS section is allowed in an EM_MIPS object.
The version field must be readable. Version zero is decoded only when its
payload is exactly 24 bytes. A malformed v0 size is rejected rather than
partially decoded.

Unknown non-zero versions remain parseable as opaque metadata consisting of
the section index, version and declared size. The parser does not reinterpret
a future layout with the v0 field offsets.

### 3. Preserve raw values and unknown bits

All v0 numeric fields remain available on MipsAbiFlagsV0. Recognized names
are convenience diagnostics:
- register widths and FP ABI;
- ISA level;
- ISA extension, including AFL_EXT_5900 = 6;
- recognized ASE bits.

Unknown ASE bits, flags1 and flags2 are preserved as raw words. The parser
does not enforce that a particular set of values is valid for a PS2 title.

### 4. Keep metadata separate from runtime admission

The section does not make an ELF executable, does not apply MIPS relocations,
and does not prove the frontend implements every instruction or ASE listed.
The initial EE RAM image builder's existing MIPS ET_EXEC/PT_LOAD restrictions
are unchanged.

## Validation

Synthetic fixtures cover:
- a 24-byte version-zero section with R5900 extension value 6;
- register size and FP ABI decoding;
- recognized and unclassified ASE/flag bits;
- an unknown future version that remains parseable but uninterpreted;
- malformed version-zero payload size;
- duplicate ABI-flags sections;
- non-MIPS machine files retaining opaque section metadata.

No retail ELF, ISO, BIOS or proprietary binary is added.
