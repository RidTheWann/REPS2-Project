# ADR 0146: Classify the alternate MIPS n32 ABI for PS2 compatibility

- **Status:** Proposed
- **Date:** 2026-11-15
- **Scope:** MIPS ELF `e_flags` compatibility diagnostics

## Context

The MIPS ELF parser already recognizes `EF_MIPS_ABI2` as a low-order feature
bit, but the compatibility report treated every recognized feature as
informational. That hid an ABI marker that changes the calling convention from
the PS2 EE profile REPS2 is intended to analyze.

## Evidence

- The GNU Binutils MIPS ELF definitions assign `EF_MIPS_ABI2` the value
  `0x00000020` and describe it as the new ABI (`-n32` on IRIX 6):
  <https://github.com/RTEMS/sourceware-mirror-binutils-gdb/blob/master/include/elf/mips.h>
- ADR 0039 records the PS2 EE example `e_flags = 0x20924001`, whose ABI field
  is EABI64 and whose ELF class remains ELF32.
- The inspected PS2SDK ELF loader checks ELF identity, class, endianness,
  object type, machine and header sizes, but does not check `e_flags`.

The first two observations establish an alternate ABI marker and the PS2
profile used by REPS2. They do not establish that the PS2SDK loader rejects an
ELF carrying the bit.

## Decision

- Report `EF_MIPS_ABI2` as an incompatible PS2 EE compatibility finding because
  REPS2 has no evidence that the n32 calling convention matches the PS2 EE
  EABI64 profile.
- Block compatibility confidence, not parsing. Preserve the raw ELF metadata
  and continue to keep parser admission separate from compatibility analysis.
- Do not emit a duplicate generic advisory for the same recognized `abi2` bit.
- Leave other ABI bits and the broader PS2-specific ABI rules unchanged until
  they have equivalent evidence.

## Validation

A synthetic ELF compatibility test sets `EF_MIPS_ABI2` and checks the stable
diagnostic code, severity, blocked stage, raw bit value, and `ps2_compatible`
result. No retail ELF or copyrighted material is included.
