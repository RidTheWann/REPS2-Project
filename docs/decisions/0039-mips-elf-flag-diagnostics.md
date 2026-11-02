# ADR 0039: Diagnostic MIPS CPU, ABI and ISA decoding from ELF e_flags

- **Status:** Accepted
- **Date:** 2026-11-02
- **Scope:** ELF32 MIPS header metadata, elf-info diagnostics, and parser regression tests

## Context

The ELF reader already exposed e_machine and the raw e_flags word, but
elf-info reported only that the machine was MIPS. That is insufficient to
distinguish a generic MIPS object from a file carrying the R5900 processor
marker used by PlayStation 2 Emotion Engine executables.

The goal is metadata classification, not a compatibility gate. Public
PS2-related tooling documents the R5900 code in the MIPS CPU field, but the
available loader evidence does not justify rejecting or accepting a game based
on e_flags alone.

## Specification and evidence

MIPS e_flags uses separate fields for low-order feature bits, the ABI
identifier, the CPU subtype and the architecture level. The CPU field is
selected by mask 0x00FF0000; the R5900 code is 0x00920000 (field value 0x92).
A commonly cited example is 0x20924001, whose subfields decode to
MIPS III, R5900, EABI64 and NOREORDER.

The EABI64 value is a MIPS ABI subfield. It is not the ELF class field: the
file remains ELF32 as determined by EI_CLASS.

References:
- ReverseEngineering.wiki, PS2 EE executable format:
  https://reverseengineering.wiki/index.php/Format:PS2_EE_executable_(ELF)
- PCSX2, Elfheader.h, illustrative e_flags = 0x20924001 comment:
  https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Elfheader.h
- spimdisasm, ELF32 MIPS flag definitions:
  https://github.com/Decompollaborate/spimdisasm/blob/master/spimdisasm/elf32/Elf32Constants.py
- PS2SDK, EE ELF loader header checks:
  https://github.com/ps2dev/ps2sdk/blob/master/ee/elf-loader2/src/elf.c

## Decision

### 1. Decode known MIPS flag fields separately

The decoder exposes:
- CPU subtype bits 23..16;
- ABI bits 15..12;
- architecture-level bits 31..28;
- recognized low-order feature bits, including NOREORDER, PIC and CPIC.

### 2. Preserve unknown values

Unknown CPU, ABI and architecture codes are retained numerically. Bits outside
the currently recognized masks are returned in unknown_bits, and the raw
32-bit word remains available. New flag assignments can therefore be added
without losing input evidence.

### 3. Keep classification informational

Image::mips_elf_flags() returns metadata only for EM_MIPS images.
elf-info prints the raw word, the decoded fields, recognized feature names
and unclassified bits. It annotates CPU code 0x92 as the R5900 marker.

The parser does not reject an ELF because the R5900 marker is absent or an
unknown flag is present. It does not infer playability or guarantee runtime
compatibility from this metadata.

### 4. Keep ELF class separate from ABI flags

EI_CLASS continues to require ELF32. An EABI64 value in the MIPS ABI
field is reported as the encoded ABI tag and never changes the parser's ELF32
layout or memory model.

## Validation

Synthetic tests pin:
- 0x20924001 -> R5900 / EABI64 / MIPS III / NOREORDER;
- preservation of unknown CPU, ABI, architecture and residual flag values;
- refusal to apply MIPS e_flags interpretation when e_machine is not MIPS.

No copyrighted ELF, game ISO, BIOS or third-party source code is included.
The diagnostics do not replace retail-ELF validation or a real-title test.
