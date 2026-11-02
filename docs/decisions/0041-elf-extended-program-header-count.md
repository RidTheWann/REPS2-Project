# ADR 0041: ELF PN_XNUM extended program-header count

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** ELF32 program-header table count resolution and bounded allocation

## Context

The parser already supported extended section counts and section-name indices
through section-header zero, and SHT_SYMTAB_SHNDX for extended symbol section
indices. It still rejected e_phnum == PN_XNUM, leaving one of the three
extended-numbering fields in section-header zero unsupported.

The remaining encoding is independent of sh_size and sh_link: it uses
section-header zero's sh_info member.

## Specification basis

The ELF ABI specifies that when the program-header table has at least
PN_XNUM (0xffff) entries, e_phnum contains the sentinel and the actual
count is stored in sh_info of section-header entry zero. Otherwise that
sh_info field is zero. The section-header entry zero is therefore required
to resolve a PN_XNUM count; an ELF with no section table cannot use the
extended encoding.

References:
- Linux man-pages, elf(5), e_phnum and PN_XNUM:
  https://man7.org/linux/man-pages/man5/elf.5.html
- Linux kernel UAPI ELF definitions, extended numbering comment and PN_XNUM:
  https://github.com/torvalds/linux/blob/master/include/uapi/linux/elf.h
- System V ABI AMD64 supplement, extended program-header count:
  https://refspecs.linuxfoundation.org/elf/x86_64-abi-0.99.pdf

## Decision

### 1. Resolve PN_XNUM from section-header zero

When e_phnum == PN_XNUM, require a section table and read the actual
program-header count from section zero's sh_info. The actual value must be
at least PN_XNUM. If e_phnum is an ordinary value, section zero's sh_info
must be zero.

This validation composes with the existing extended fields: section zero may
simultaneously carry sh_size for extended e_shnum, sh_link for extended
e_shstrndx, and sh_info for PN_XNUM.

### 2. Bound allocations before reading entries

ParseLimits gains max_program_headers with a default of 100,000. The
resolved 32-bit count is converted to host size and checked against that limit
before allocating the program-header vector or validating the complete table
range. This supports the ELF32 PN_XNUM threshold while preserving a bounded
allocation policy.

### 3. Preserve other format boundaries

This change does not add 64-bit ELF, R_MIPS relocation application, dynamic
linking, PS2-specific ABI enforcement, or any claim that an unusually large
program-header table corresponds to a retail PS2 executable.

## Validation

A synthetic ELF fixture contains exactly PN_XNUM program-header entries,
uses e_phnum == PN_XNUM, and stores the actual count in section zero's
sh_info. The parser verifies the complete table and retains every entry.

Negative tests cover:
- sh_info below the PN_XNUM threshold;
- nonzero sh_info when e_phnum is not PN_XNUM;
- PN_XNUM without a section table;
- a max_program_headers limit below the resolved count.

The same parser test suite runs on Windows/MSVC and Linux/GCC; the workspace
CI also tests the declared Rust 1.88 MSRV. No proprietary ELF or ISO is added.
