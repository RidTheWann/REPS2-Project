# ADR 0042: ELF identification and object-type diagnostics

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** ELF32 e_ident OS ABI/version metadata, e_type classification, and elf-info diagnostics

## Context

The parser already retained the numeric e_type but did not preserve e_ident[EI_OSABI]
or e_ident[EI_ABIVERSION]. The CLI showed the raw object-type number and added
an executable label only for ET_EXEC. This made it difficult to inspect
relocatable/shared/core objects and identify generic OS ABI metadata without
mistaking a label for actual platform compatibility.

## Specification basis

The System V ABI identifies:
- EI_OSABI at e_ident[7];
- EI_ABIVERSION at e_ident[8];
- standard e_type values ET_NONE, ET_REL, ET_EXEC, ET_DYN, and ET_CORE;
- ET_LOOS..ET_HIOS as OS-specific and ET_LOPROC..ET_HIPROC as processor-specific.

The ABI version byte is interpreted in the context of EI_OSABI. OSABI meanings
in the 64..255 range can be defined by the architecture's processor supplement.

References:
- System V ABI, ELF Header / ELF Identification:
  https://web.stanford.edu/~ouster/cgi-bin/cs140-spring18/pintos/specs/sysv-abi-update.html/ch4.eheader.html
- Oracle Linkers and Libraries Guide, EI_ABIVERSION interpretation:
  https://docs.oracle.com/cd/E19683-01/817-1974/817-1974.pdf
- Binutils ELF constants:
  https://gnu.googlesource.com/binutils-gdb/+/b4812cfe4dc394392e47e0489f38521da4a0b7ce/include/elf/common.h

## Decision

### 1. Preserve raw identification metadata

The parsed Image stores os_abi and abi_version directly from e_ident[7] and
e_ident[8]. Neither byte is treated as a validation gate. Values outside the
generic labels remain intact for later machine-specific interpretation.

### 2. Provide generic OS ABI labels

The generic mapping names System V/unspecified (0), HP-UX (1), NetBSD (2),
Linux (3), Solaris (6), AIX (7), IRIX (8), FreeBSD (9), Tru64 (10),
Novell Modesto (11), OpenBSD (12), OpenVMS (13), and NonStop Kernel (14).
Values 64..=255 are reported as architecture-specific. Other unassigned
values are reported as reserved/unrecognized. The mapping is descriptive
only; machine supplements can assign meanings in the architecture-specific
range.

### 3. Classify object types without rejecting variants

e_type values 0..=4 receive their standard labels. Values in
0xFE00..=0xFEFF are labeled OS-specific; values in 0xFF00..=0xFFFF are
processor-specific; other unassigned values are labeled reserved/unknown.
The raw u16 remains available on Image.

The current main-RAM builder still accepts only its separately documented
static ET_EXEC/PT_LOAD model. Reporting ET_REL, ET_DYN, ET_CORE, or a
processor-specific type does not add loader or runtime support.

### 4. Keep ABI version interpretation contextual

The CLI prints the raw ABI-version byte and its associated OSABI code. It
does not invent a version name when the OSABI processor supplement has not
been implemented.

## Validation

Synthetic tests cover:
- EI_OSABI FreeBSD and a nonzero EI_ABIVERSION preserved together;
- an architecture-specific OSABI code retained without rejection;
- an unrecognized OSABI code retained without rejection;
- all five standard e_type values;
- both ends of OS-specific and processor-specific e_type ranges;
- an otherwise reserved e_type code, preserved and classified as unknown.

No retail ELF, game ISO, BIOS, or proprietary binary is added. No parser
accept/reject rule or title compatibility claim changes.
