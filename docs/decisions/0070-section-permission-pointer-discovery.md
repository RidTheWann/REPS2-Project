# ADR 0070: Section-declared executable ranges drive pointer-table discovery

- **Status:** Accepted
- **Date:** 2026-11-06
- **Scope:** M1 executable intelligence, M2 static function discovery

## Context

`--discover` recovers function entry points for stripped retail executables from
three sources: `JAL` call targets, canonical `addiu $sp, $sp, -N` prologues, and
stored code pointers read from file-backed data words. The pointer scan decided
what counted as data using the program header: every `PT_LOAD` with `PF_X` set was
treated as code and skipped entirely.

Retail PS2 executables commonly place every loadable byte in a single `PT_LOAD`
with `PF_R | PF_W | PF_X`. Downhill Domination (`SCUS_971.77`) is one: one
loadable segment covering `.text`, `.vutext`, `.data`, `.rodata` and `.sdata`.
For that image the pointer scan therefore examined **zero** words.

The consequence was observed in the bounded native probe. Execution serviced
`FindAddress` (0x83) at `0x0025EF7C` and then stopped at `0x0025EF38` with
`unresolved_control_flow`. Direct inspection of the title showed that
`0x0025EF38` is a real leaf routine in `.text`:

```
0025ef38: 8c820000   lw   $v0, 0($a0)
0025ef3c: 1046000b   beq  $v0, $a2, ...
0025ef40: 0085102b   sltu $v0, $a0, $a1
...
0025ef70: 03e00008   jr   $ra
0025ef74: 0080102d   move $v0, $a0
```

It has no stack frame and no `JAL` references it, so neither existing discovery
source could see it. It appears exactly once in the image as a data word, in a
genuine handler-descriptor table in `.data`:

```
00282218: 00000083   # syscall number 0x83 (FindAddress)
0028221c: 0025ef38   # handler address
00282220: 0000005a   # syscall number 0x5A
00282224: 0025ef00   # handler address
```

The information needed to separate those words from code was already present:
the section table records `.text`/`.vutext` as `SHF_ALLOC | SHF_EXECINSTR` and
`.data`/`.rodata`/`.sdata` as allocated but not executable.

## Decision

`discover_function_roots` takes a list of section-declared executable virtual
address ranges, computed by the new `executable_section_ranges` helper from
allocated `SHF_EXECINSTR` sections (sorted and merged). The pointer scan then:

- reads every file-backed word that is **not** inside one of those ranges, and
- falls back to the previous program-header rule (skip `PF_X` segments) when the
  section table declares no executable section at all.

Only words that are naturally aligned, lie in a file-backed segment, and resolve
into a file-backed executable segment become candidates. Candidates remain
discovery guesses: the CFG builder still drops any candidate that cannot be
decoded and reports it, and none of them is a reachability claim.

## Consequences

- Downhill Domination discovery now scans 49,148 data words instead of 0 and
  admits 3,812 pointer roots (8,400 accepted roots, 61,333 basic blocks,
  314,561 decoded instructions). `0x0025EF38` is admitted as
  `function: 0x0025EF38 (discovered)` and translated.
- Images that already separate code and data with program headers behave as
  before, because their data segments are scanned exactly as they were.
- The scan is bounded by the existing `--max-roots` limit; the bound is still
  enforced through `candidate_count()`.
- Code words are never read as pointers, so instruction encodings that happen to
  resemble guest addresses cannot add spurious roots. This is covered by a
  regression test with a deliberately pointer-shaped code word.

## Evidence

- `analysis::tests::section_permissions_separate_code_from_pointer_tables_in_one_rwx_segment`
  builds one `RWX` `PT_LOAD` whose section table marks `.text` executable and
  `.data` writable, places a handler descriptor in `.data`, and asserts the
  handler becomes a candidate root and a CFG block while the pointer-shaped code
  word is ignored.
- `analysis::tests::data_pointers_in_non_executable_segments_are_candidate_roots`
  still passes, pinning the program-header fallback.
- Real-title analysis and probe measurements are recorded in
  `docs/PROJECT_STATE.md`.
