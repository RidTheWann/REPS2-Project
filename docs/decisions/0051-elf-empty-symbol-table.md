# ADR 0051: Accept empty ELF symbol tables

- **Status:** Accepted
- **Date:** 2026-11-03
- **Scope:** ELF32 SHT_SYMTAB/SHT_DYNSYM admission policy

## Context

ADR 0038 required every symbol table to contain at least one entry, a null
symbol at index zero, and a nonzero `sh_info` local-symbol boundary. That
invariant is too strict. The System V generic ABI permits a symbol table with
`sh_size == 0`: such a table has no local symbols, no entry zero, and therefore
`sh_info == 0`.

Two additional retail PS2 boot ELFs (Bully `SLUS_212.69` and Grand Theft Auto:
San Andreas `SLUS_209.46`) carry exactly this shape: a `SHT_SYMTAB` section
with `sh_size = 0`, `sh_info = 0`, `sh_entsize = 16`, linked to an empty
`SHT_STRTAB`. The previous parser rejected both images with
`ELF symbol table has an invalid local-symbol boundary` before any code
analysis could run, even though their program headers and executable sections
were otherwise valid.

## Decision

- Accept a symbol table with `sh_size == 0` when `sh_info == 0`.
- Reject an empty table with nonzero `sh_info`, because the boundary would
  name a local symbol that does not exist.
- Continue to reject a non-empty table with `sh_info == 0`, because entry zero
  is the mandatory null symbol and must be local.
- Skip entry decoding for empty tables; the null-symbol and per-symbol checks
  apply only when entries exist.
- Continue to validate the section entry size, linked string-table type,
  resource limits, and any linked `SHT_SYMTAB_SHNDX` entry-count consistency
  before accepting an empty table.

## Consequences

Stripped retail executables with empty symbol tables now reach CFG analysis and
code generation. Symbol-driven root discovery naturally contributes nothing
for these images, so static `JAL`/prologue discovery remains the fallback
defined by ADR 0026. Malformed empty tables with a nonzero boundary or a
mismatched extended-index table remain rejected.

## Validation

- Synthetic ELF test accepts `sh_size = 0` with `sh_info = 0` and yields zero
  symbols and relocations.
- Synthetic ELF test rejects `sh_size = 0` with `sh_info = 1`.
- End-to-end local validation: `extract-boot-elf`, `elf-info`,
  `analyze-elf --discover`, and `recompile-elf` complete with exit code 0 for
  both retail boot ELFs above.

No game or proprietary binary is committed to the repository.
