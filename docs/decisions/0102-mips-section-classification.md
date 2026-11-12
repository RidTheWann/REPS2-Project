# ADR 0102: MIPS processor-specific section classification

- **Status:** Accepted
- **Date:** 2026-11-09
- **Scope:** Rust frontend ELF compatibility diagnostics (`rust/frontend/src/elf.rs`, `rust/frontend/src/elf_compat.rs`)
- **Depends on:** ADR 0039 (MIPS ELF flag diagnostics), ADR 0043 (`.reginfo`/`.MIPS.abiflags` diagnostics), ADR 0044 (`SHT_MIPS_OPTIONS` diagnostics), ADR 0086 (stable diagnostic-code catalog)

## Context

`analyze_compatibility` reports every MIPS processor-specific section it does
not interpret as an unresolved `UNKNOWN-MIPS-SECTION-TYPE` finding
(`Severity::Unknown`), which sets `has_unresolved` and adds a
`CompatibilityConfidence` blocker. Two separate loops did this: one in
`analyze_mips_compatibility` and one in `analyze_unresolved`, so a single
section produced two identical findings.

That classification is inaccurate. Many of these types are documented MIPS
psABI metadata that a linker or debug-info producer legitimately emits
(`SHT_MIPS_DWARF`, `SHT_MIPS_LIBLIST`, `SHT_MIPS_GPTAB`, ...). REPS2 does not
interpret them, but they are not "unknown", and flagging them as unresolved
overstates a title's risk.

## Evidence

- **Observed (project-local PS2 EE toolchain header,
  `ee/mips64r5900el-ps2-elf/include/elf.h`):** the documented
  `SHT_MIPS_*` types `0x70000000`..`0x70000029` and
  `PT_MIPS_REGINFO`/`PT_MIPS_ABIFLAGS`. This is the toolchain the project uses
  for synthetic fixtures, so it is the authoritative local source for these
  constants.
- **Observed (existing tests):** `unknown_mips_section_type_is_unknown` used
  `0x70000010` as its "unknown" sample; `0x70000010` is
  `SHT_MIPS_SHDR`, a documented type, so the test's premise was wrong.
- **Observed (code):** `analyze_mips_compatibility` and `analyze_unresolved`
  each contained a loop over `image.section_headers` emitting
  `UnknownMipsSectionType` for the same condition.

## Decision

1. Add `elf::mips_section_type_name(u32) -> Option<&'static str>`: the documented
   MIPS processor-specific section types with their psABI names, taken from the
   local toolchain header. Values the psABI does not define return `None`.
2. Classify MIPS processor-specific sections in exactly one place,
   `analyze_mips_compatibility`:
   - the three interpreted types (`SHT_MIPS_REGINFO`, `SHT_MIPS_ABIFLAGS`,
     `SHT_MIPS_OPTIONS`) are skipped;
   - a documented-but-uninterpreted type emits the new
     `DiagnosticCode::MipsSectionNotInterpreted` (`MIPS-SECTION-NOT-INTERPRETED`)
     at `Severity::Advisory` with the type's name and with no blocked stage;
   - a value the psABI does not define still emits
     `DiagnosticCode::UnknownMipsSectionType` at `Severity::Unknown` and blocks
     `CompatibilityConfidence`.
3. Remove the duplicate section loop from `analyze_unresolved`; its
   program-header and symbol checks stay, so each section type is reported once.
4. Update `unknown_mips_section_type_is_unknown` to use a genuinely undefined
   value (`0x7000FFFF`) and add `documented_mips_section_type_is_advisory`.

## Alternatives considered

- **Keep reporting everything as Unknown.** Rejected: it mislabels documented
  metadata and inflates the unresolved set.
- **Silently ignore all documented MIPS metadata sections.** Rejected: it would
  hide which documented sections are present; an advisory finding keeps the
  record visible while making clear it is not a blocker.
- **Parse every documented section.** Out of scope; REPS2 interprets only the
  three types its analysis consumes.

## Consequences

- Documented MIPS metadata sections no longer count as unresolved and no longer
  add a `CompatibilityConfidence` blocker; they appear once, as advisory.
- Genuinely undefined processor-specific types still fail visible as `Unknown`.
- No change to which sections REPS2 interprets or to any translation output.

## Validation

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` (336 main-suite tests, +1 new).
- CMake Release build and `ctest -C Release` (33/33).
- New/updated unit tests: `documented_mips_section_type_is_advisory` asserts the
  advisory severity, the recognized name, no blocked stage and no duplicate
  unknown finding for `SHT_MIPS_DWARF`; the updated
  `unknown_mips_section_type_is_unknown` keeps the undefined-value behavior.

## Limitations and next evidence

- The name table is metadata recognition only; none of these sections are
  parsed or used by analysis.
- It covers the types defined by the local toolchain header; a future psABI
  addition would appear as `UnknownMipsSectionType` until added.
- This is a diagnostics-quality change; it does not advance real-title execution
  and makes no boot or Playable claim.