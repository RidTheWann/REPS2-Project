# ADR 0030: Explicit analysis-limit policy and CLI overrides

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** `AnalysisLimits` consumption, `analyze-elf`/`recompile-elf` limit flags, limit diagnostics, and manifest recording

## Context

ELF analysis is deliberately bounded so an untrusted or unusually large image
cannot force unbounded work. The defaults are 250,000 basic blocks, 2,000,000
decoded instructions and 500,000 discovered roots. Static discovery on
`GTA San Andreas` already produces 213,942 blocks and 1,040,666 decoded
instructions, which leaves only about 15% headroom on the block bound. Larger
executables, different revisions, or a more permissive discovery pass would hit
the default and fail with no way to proceed short of editing the source.

The failure mode was explicit but not actionable: `BlockLimitExceeded` and
`InstructionLimitExceeded` named only the number, and the hard-coded defaults
were the same for every command. The manifest already records the applied
limits, so raising a bound would also leave a truthful artifact.

## Decision

### 1. Keep the defaults

The default bounds stay unchanged. They are safe for an untrusted input and the
largest sampled title still fits. Raising defaults globally would remove the
safety property for no measured need.

### 2. Developer-CLI overrides

`analyze-elf` and `recompile-elf` accept:

- `--max-blocks N`
- `--max-instructions N`
- `--max-roots N` (the static-discovery candidate budget)

The flags are order-independent and may be repeated; the last value wins.
`compile-cfg-fixture` and the library importer keep their fixed defaults
because they operate on synthetic or already-validated local inputs.

### 3. Validation and ceilings

Every override must be a positive integer and must not exceed a CLI ceiling:

| limit | default | ceiling |
| --- | ---: | ---: |
| basic blocks | 250,000 | 16,777,216 (2^24) |
| decoded instructions | 2,000,000 | 33,554,432 (2^25) |
| discovered roots | 500,000 | 16,777,216 (2^24) |

A 32 MiB EE main-RAM image contains at most 8,388,608 aligned words, so the
ceilings exceed every physically reachable count by at least a factor of two
(the instruction ceiling covers duplicate accounting across aliases and
delay-slot paths). They remain a hard bound on caller-selected work rather than
an invitation to disable the safety property. Zero, negative, non-numeric and
above-ceiling values are rejected before analysis starts.

### 4. Actionable diagnostics

Block and instruction limit failures append the relevant flag to the analyzer's
original message, for example:

    analysis exceeded the 1000 basic-block limit; raise --max-blocks above 1000 to analyze more

Discovery-candidate truncation remains non-fatal and is already reported through
`truncated: true`; the override lets a caller raise the root budget when that
happens.

### 5. Visibility

`analyze-elf` prints the applied limit triple. `recompile-elf` prints it beside
the build manifest path, and the existing manifest `analysis.limits` section
already records every applied bound. Overridden runs therefore remain
self-describing and deterministic.

## Validation

Rust tests:

- `reports_configured_block_and_instruction_limits` proves a lowered block or
  instruction bound fails with the matching typed error.
- `parses_analyze_options_and_overrides` and `parses_recompile_options_in_any_order`
  cover flag parsing and default preservation.
- `rejects_invalid_analysis_limit_overrides` covers missing, zero, non-numeric
  and above-ceiling values for all three flags.
- `limit_options_preserve_unrelated_defaults` proves overrides do not change
  `max_loaded_memory` or `max_aliased_roots`.

The full Rust suite is 158 library + 8 binary tests; `cargo fmt --check` and
`cargo clippy -D warnings` are clean. CTest is 15/15.

Manual verification against `build/isoaudit/Black_USA_iso.elf`:

- `analyze-elf --discover --max-blocks 1000` fails with the hint above.
- `analyze-elf --discover --max-blocks 300000 --max-instructions 2500000`
  reports the applied limits, 97,678 blocks, 473,292 decoded instructions and
  1,471 unsupported stops, matching the default-limit analysis.
- `recompile-elf --max-blocks 300000 --max-instructions 2500000` records
  `max_blocks: 300000`, `max_instructions: 2500000` and
  `max_function_roots: 500000` in `policy.manifest.json` while still writing
  `playable: false`.

This validates limit selection and reporting, not the semantic correctness of
discovered blocks or title playability.

## Consequences

- Larger executables can be analyzed and recompiled without a source change or
  a silent loss of coverage.
- The defaults remain the safe path, and every override is explicit, bounded,
  printed, and recorded in the artifact.
- A caller that raises a limit accepts the corresponding time and memory cost;
  the toolchain does not guess a size policy on their behalf.
- Automatic size-derived limits remain possible later without changing the
  flag contract.