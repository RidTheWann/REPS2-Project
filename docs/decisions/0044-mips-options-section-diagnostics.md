# ADR 0044: MIPS SHT_MIPS_OPTIONS descriptor diagnostics

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** Bounded, read-only parsing of ELF32 SHT_MIPS_OPTIONS for EM_MIPS objects

## Context

REPS2 already exposes MIPS SHT_MIPS_REGINFO and versioned SHT_MIPS_ABIFLAGS
metadata. The MIPS ABI also defines SHT_MIPS_OPTIONS (0x7000000d), which is a
stream of variable-length option descriptors. Ignoring it leaves relevant
register-usage and option metadata undisclosed; treating the section as a
fixed-size array would misparse legitimate variable records and malformed
lengths could advance beyond the section.

## Specification basis

The ELF32 option header is eight bytes:
- \`kind\`: one-byte descriptor kind;
- \`size\`: one-byte total record size, including the header;
- \`section\`: 16-bit affected section-header index, or zero for global options;
- \`info\`: 32-bit kind-specific metadata.

GNU ELF headers define ODK_NULL through ODK_HWOR as kinds 0..8. LLVM's MIPS
ELF definitions also name ODK_GP_GROUP, ODK_IDENT and ODK_PAGESIZE (9..11).
For ODK_REGINFO, GNU Binutils readelf interprets the data immediately after
the eight-byte header as ELF32_RegInfo: four-byte GPR mask, four 32-bit
coprocessor masks, and a signed 32-bit gp value (24 bytes total).

References:
- GNU ELF header: SHT_MIPS_OPTIONS, Elf_Options and ODK values:
  https://kernel.googlesource.com/pub/scm/utils/kernel/kexec/kexec-tools/+/master/include/elf.h
- GNU Binutils readelf: variable-size descriptor traversal and ELF32 ODK_REGINFO:
  https://gnu.googlesource.com/binutils-gdb/+/b20a3440c9eb428a2e89efab2ec9fa1a39bd6a78/binutils/readelf.c
- LLVM ELF.h MIPS option-kind definitions:
  https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/BinaryFormat/ELF.h

## Decision

### 1. Decode the section only for EM_MIPS

Non-MIPS objects retain their section headers but do not have processor-specific
MIPS option records interpreted. This matches the existing .reginfo and
.MIPS.abiflags policy.

### 2. Parse a bounded descriptor stream

The parser walks the section bytes sequentially. Every record must have the
complete eight-byte header, a total size of at least eight bytes, and an end
offset that stays within the section. A nonzero affected-section index must
refer to a section in the parsed section table.

ParseLimits gains \`max_mips_option_descriptors\`, default 100,000, checked
before each descriptor is retained. This prevents a tiny-record stream from
creating an unbounded metadata vector. The parser does not copy variable
opaque payloads into Image; it preserves each record's kind, total size,
affected-section index, info word and payload length, leaving raw bytes in the
owner-provided ELF file.

### 3. Decode only the well-defined ELF32 ODK_REGINFO payload

ODK_REGINFO (kind 1) must contain at least 24 bytes after its header. Those
bytes are decoded as the 32-bit ELF_RegInfo shape: GPR mask, four coprocessor
masks and signed gp value. Additional bytes remain opaque and their overall
length is still reported. No other kind-specific payload is guessed.

Known descriptor kinds receive generic labels. Unknown kinds retain their
numeric value and are labelled unknown; they are not rejected solely for being
unrecognized.

### 4. Preserve diagnostic-only policy

MIPS option records are metadata supplied by the object producer. Their
presence, absence, info bits or register-use masks are not used as a proof of
R5900 ISA compatibility, title compatibility, or permission to execute a
memory/device operation. No instruction, relocation, system service, or MMIO
behavior is implemented by this parser extension.

## Validation

Synthetic in-memory ELF32 fixtures cover:
- an ODK_REGINFO header plus its full 24-byte payload;
- an ODK_EXCEPTIONS descriptor with raw info retained;
- an unknown descriptor kind with opaque trailing bytes summarized by length;
- global and section-targeted descriptors;
- truncated headers, size below eight, descriptor overruns, short ODK_REGINFO
  payloads, invalid affected-section indices, and descriptor-count limits;
- non-EM_MIPS files whose MIPS options payload is deliberately malformed.

No retail ELF, ISO, BIOS, or proprietary game data is added.
