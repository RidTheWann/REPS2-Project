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
allocated `SHF_EXECINSTR` sections (sorted and merged). When that list
intersects the loaded segments, it decides both what is scanned as code and what
is admitted as a candidate:

- the `JAL`/prologue scan reads only words inside those ranges, so `.data`
  bytes are never decoded as instructions;
- the pointer scan reads every file-backed word **outside** those ranges; and
- a candidate is admitted only when it is file-backed executable **and** inside
  a section-declared executable range.

When the section table declares no executable section, or none of its ranges
intersects a loaded segment, the previous program-header rule applies unchanged:
`PF_X` segments are scanned as code, and non-`PF_X` segments are scanned for
pointers.

Only words that are naturally aligned, lie in a file-backed segment, and resolve
into a file-backed executable segment become candidates. Candidates remain
discovery guesses: the CFG builder still drops any candidate that cannot be
decoded and reports it, and none of them is a reachability claim.

## Consequences

- Downhill Domination discovery now scans 49,148 data words instead of 0 and
  admits 2,491 pointer roots (7,144 accepted roots, 59,922 basic blocks,
  309,505 decoded instructions). `0x0025EF38` is admitted as
  `function: 0x0025EF38 (discovered)` and translated.
- Because the same ranges gate the `JAL`/prologue scan, `.data` words are no
  longer decoded as instructions: unsupported blocks fall from 1,874 to 784 and
  skipped heuristic blocks from 102 to 24 on the same image.
- Images without a usable section table keep the program-header rule, so their
  discovery results are unchanged. Images that do carry section permissions gain
  a data-word census as well, which can change `scanned_data_words` and root
  counts even when the segment flags already separated code from data.
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
