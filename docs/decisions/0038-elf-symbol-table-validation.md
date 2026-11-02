# ADR 0038: ELF string, symbol-table and section cross-reference validation

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** ELF32 section, string, symbol and relocation metadata validation

## Context

The frontend already bounded ELF header and table reads, decoded section names,
parsed symbols, and checked REL/RELA links and symbol indices. However, several
cross-table invariants from the System V ELF ABI were not yet enforced:

- non-empty string tables could lack their leading or trailing NUL sentinel;
- section index zero was not required to be the null section header;
- a symbol's ordinary st_shndx could point past the section table;
- sh_info on a symbol table was not checked against the local/non-local
  symbol partition;
- symbol-table entry zero was not verified as the all-zero undefined symbol.

These malformed structures could be surfaced as parsed metadata and only fail
later, or be misinterpreted by consumers.

## Specification basis

The implementation follows the System V Generic ABI:

- section-header index zero is the mandatory SHT_NULL entry;
- SHT_SYMTAB/SHT_DYNSYM sh_info identifies the first non-local symbol;
- SHT_REL/SHT_RELA sh_link identifies the symbol table and sh_info
  identifies the relocation target section;
- non-empty string tables begin and end with a NUL byte;
- ordinary symbol section indices refer to section headers, while reserved
  indices such as SHN_ABS/SHN_COMMON are special values rather than indices.

References:
- System V Generic ABI, section headers, sections 3.5-3.6:
  https://gabi.xinuos.com/v42/elf/03-sheader.html
- System V ELF string-table rules:
  https://refspecs.linuxfoundation.org/elf/elfspec.pdf

## Decision

### 1. Validate SHT_NULL section zero

When a section table is present, entry zero must be SHT_NULL and its ordinary
fields must be zero. Extended section numbering is still rejected explicitly,
so non-zero sh_size/sh_link extension fields are not accepted.

### 2. Validate string-table sentinels

Every non-empty SHT_STRTAB must begin and end with NUL. Empty string tables
remain legal. This is applied to section-name tables and symbol-linked string
tables before resolving any names.

### 3. Validate symbol-table ordering and ownership

A SHT_SYMTAB/SHT_DYNSYM must contain the all-zero undefined symbol entry and a
valid sh_info boundary. Symbols before sh_info must use STB_LOCAL; symbols at
or after it must not. Ordinary st_shndx values below SHN_LORESERVE must
reference an existing section. Reserved OS/processor values and SHN_ABS/
SHN_COMMON remain representable; SHN_XINDEX remains explicitly unsupported.

### 4. Keep relocation application separate

REL/RELA parsing retains its existing cross-checks: sh_link must identify
a symbol table, sh_info must identify an existing target section, and each
relocation symbol index must be within the linked table. This change does not
apply R_MIPS relocation types or reinterpret addends.

## Validation

Synthetic ELF tests cover:
- malformed leading/trailing string-table sentinels;
- malformed section-header zero;
- out-of-range ordinary symbol section indices;
- acceptance of the special SHN_ABS index;
- a non-zero symbol-table entry zero;
- symbol-table sh_info beyond the table and a boundary that incorrectly
  classifies a global symbol as local;
- existing REL and PT_LOAD materialization behavior.

No retail game or proprietary ELF is committed. Extended symbol indices,
PS2-specific ABI metadata, and MIPS relocation application remain separate work.
