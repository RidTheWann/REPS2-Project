# ADR 0042: ELF OSABI, ABI version and object-variant diagnostics

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** ELF32 e_ident OSABI/ABI version preservation, generic e_type/e_machine labels, and elf-info output

## Context

The parser already preserved e_type, e_machine and MIPS e_flags, but
discarded e_ident[EI_OSABI] and e_ident[EI_ABIVERSION]. The CLI printed a
numeric file type and only a MIPS/non-MIPS machine suffix, which made unusual
ELF variants harder to identify during research.

The parser deliberately accepts metadata outside the default PS2 loader path.
That behavior should remain: parsing a relocatable or shared-object ELF is
not the same as supporting its relocation or dynamic-loading semantics.

## Specification basis

The ELF ABI defines:
- EI_OSABI as the target operating-system/ABI identifier;
- EI_ABIVERSION as a version interpreted in the context of EI_OSABI;
- e_type values for none, relocatable, executable, shared-object and core
  objects, with OS/processor-specific ranges reserved separately.

References:
- Linux man-pages, elf(5), ELF identification and object type:
  https://www.man7.org/linux/man-pages/man5/elf.5.html
- Oracle Linker and Libraries Guide, ELF identification fields:
  https://docs.oracle.com/cd/E23824_01/html/819-0690/chapter6-35342.html

## Decision

### 1. Preserve raw OSABI and ABI version

Image now stores the exact EI_OSABI and EI_ABIVERSION bytes. Common generic
OSABI codes have display names; unrecognized values remain numerically
available and do not cause parse failure.

### 2. Diagnose generic ELF variants

Image exposes readable labels for common e_type values and identifies
OS-specific and processor-specific e_type ranges. It also labels common
e_machine identifiers, retaining the raw machine value for every object.

### 3. Keep diagnostics separate from load support

elf-info reports the raw values and diagnostic labels. The parser does not
reject ET_REL, ET_DYN, ET_CORE, non-MIPS machine IDs or unknown OSABI codes
solely because they are not current PS2 EE load targets.

The default EE initial RAM image builder remains limited to the supported
MIPS ET_EXEC path. ET_REL relocation application and ET_DYN loading remain
unsupported. A diagnostic label is not a compatibility result.

## Validation

Synthetic tests verify:
- EI_OSABI/EI_ABIVERSION preservation;
- a common GNU/Linux OSABI label and an unrecognized OSABI that still parses;
- ET_NONE, ET_REL, ET_EXEC, ET_DYN, ET_CORE, OS-specific, processor-specific
  and unknown e_type labels;
- common and unknown e_machine labels.

No game ELF, ISO, BIOS or proprietary binary is included. No title
compatibility claim is made.
