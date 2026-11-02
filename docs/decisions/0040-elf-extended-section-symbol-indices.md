# ADR 0040: ELF extended section and symbol indices

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** ELF32 e_shnum/e_shstrndx extended numbering and SHT_SYMTAB_SHNDX resolution

## Context

The ELF32 parser previously rejected every SHN_XINDEX symbol and every ELF
extended section-number encoding. These are two parts of the same indexing
scheme: an extended symbol index cannot be resolved unless the parser can
represent the section table containing that index.

The parser also needs a resource bound because extended section counts are
32-bit values rather than the previous 16-bit e_shnum field.

## Specification basis

The System V ABI defines:
- section counts at or above SHN_LORESERVE (0xff00) by setting e_shnum to
  SHN_UNDEF and storing the actual section count in section-header zero's
  sh_size;
- section-name table indices at or above SHN_LORESERVE by setting e_shstrndx
  to SHN_XINDEX and storing the actual index in section-header zero's sh_link;
- SHT_SYMTAB_SHNDX as an Elf32_Word array with one entry per symbol, in the
  same order as its associated SHT_SYMTAB/SHT_DYNSYM;
- a nonzero extended-index entry only when the corresponding symbol's
  st_shndx is SHN_XINDEX; otherwise the corresponding entry must be SHN_UNDEF;
- SHT_SYMTAB_SHNDX sh_link to the associated symbol table and sh_info zero.

References:
- System V ABI, section-header extended numbering and special section indices:
  https://web.stanford.edu/~ouster/cgi-bin/cs140-spring18/pintos/specs/sysv-abi-update.html/ch4.sheader.html
- Oracle Linkers and Libraries Guide, SHT_SYMTAB_SHNDX entry correspondence:
  https://docs.oracle.com/cd/E19683-01/816-7777/816-7777.pdf
- Linux man-pages, ELF header extended section count and name-table index:
  https://www.kernel.org/pub/linux/docs/man-pages/man5/elf.5.html

## Decision

### 1. Resolve extended section numbering first

If e_shoff is zero, e_shnum and e_shstrndx must both be zero. Otherwise
section-header zero is read after validating the table's first entry.

For e_shnum zero, section zero's sh_size must hold a count at or above
SHN_LORESERVE. For e_shstrndx SHN_XINDEX, section zero's sh_link must hold
an actual section index at or above SHN_LORESERVE. Direct values at or above
the reserved threshold are rejected when they should use an escape encoding.

The resolved section-name table index is stored as u32 in the parsed image.
Section-header zero retains its required SHT_NULL form; only sh_size and
sh_link may carry the corresponding extended-numbering values.

### 2. Bound section metadata allocation

ParseLimits gains max_sections (default 100,000). The resolved section count
is checked against this limit before allocating or reading the section-header
vector. The 100,000 default supports the ELF32 extended-index range while
keeping metadata allocation bounded. Extended program-header numbering
(PN_XNUM) remains explicitly unsupported.

### 3. Resolve SHT_SYMTAB_SHNDX per symbol table

Every SHT_SYMTAB_SHNDX must link to an existing SHT_SYMTAB or SHT_DYNSYM.
Only one such table may be associated with a symbol table. Its sh_entsize
must be four bytes, its sh_size must describe exactly one entry per symbol,
and sh_info must be zero.

For each symbol:
- st_shndx == SHN_XINDEX requires an associated table and a resolved index
  at or above SHN_LORESERVE but below the parsed section count;
- every other st_shndx requires its matching extended-index slot to be zero;
- ordinary low st_shndx values continue to reference actual section headers;
- reserved special st_shndx values such as SHN_ABS and SHN_COMMON remain
  representable.

The resulting Symbol.section_index is widened to u32 so downstream analysis
can carry the resolved value without truncation.

### 4. Preserve unsupported boundaries

This change does not add PN_XNUM, R_MIPS relocation application, dynamic
linking, PS2-specific ABI validation, or a claim that large synthetic ELF
fixtures correspond to retail game binaries.

## Validation

A synthetic ELF fixture uses:
- extended e_shnum and e_shstrndx values stored in section-header zero;
- a section at index 0xff00 referenced by a symbol with st_shndx = SHN_XINDEX;
- a linked SHT_SYMTAB_SHNDX with one entry per symbol;
- a relocation still linked to the same symbol table and target section.

Negative tests cover missing/orphan/duplicate linkage, malformed entry sizes
and counts, nonzero sh_info, invalid extended index ranges, nonzero slots for
ordinary symbols, invalid extended-numbering sentinels, and max_sections.

No retail executable, ISO, BIOS or proprietary binary is included.
