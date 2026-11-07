# ADR 0085: Keep CFG resource and invariant failures fatal on speculative paths

- **Status:** Accepted
- **Date:** 2026-11-07
- **Scope:** Reachability-guided CFG analysis and its `analyze-elf` CLI reporting

## Context

CFG discovery intentionally queues heuristic roots and post-SYSCALL continuations leniently: undecodable speculative content should be skipped and reported rather than making otherwise useful analysis fail (ADRs 0026 and 0063). The same blanket lenient `Err(_)` handling also swallowed resource and invariant failures.

A synthetic sequence `syscall; nop; nop` with `max_instructions = 1` returned a graph reporting two decoded instructions: the authoritative SYSCALL used the only slot, then the lenient continuation decoded both NOPs before returning a successful partial graph. Separately, `increment_instruction_count` incremented before detecting exhaustion, so the rejected instruction changed the count. A later block split checked its limit after mutating the graph, and lenient split failures were also blanket-swallowed.

## Decision

1. Classify errors through one exhaustive `AnalysisError` match. Only speculative content/ownership-boundary errors that describe an undecodable delay-slot target (`MissingDelaySlot`, `UnsupportedDelaySlot`, `ControlTransferInDelaySlot`, `TargetEntersDelaySlot`) may be skipped for lenient roots/continuations. Resource limits, ELF/segment validation, PC overflow, and internal ownership failures remain fatal.
2. Preserve the existing continuation behavior: valid code after SYSCALL is translated; a speculative continuation whose delay-slot structure is unsupported remains absent and reported through the existing skipped-block path.
3. Check instruction capacity before incrementing, so failure does not mutate the count and `usize::MAX` cannot overflow.
4. Check block capacity before splitting a block. Split ownership errors are invariants and propagate rather than being treated as speculative decode failures.
5. Do not expand the public skipped-block or analysis result API for this fix.

## Validation

A regression test first failed against the old behavior: `syscall; nop; nop` with limit 1 returned `Ok` and `decoded_instruction_count = 2`; the counter boundary test also observed a mutated count after refusal. After the fix, tests cover syscall continuation budgets 1/2/3, heuristic and authoritative roots, ordinary instructions and delay slots, explicitly tolerated speculative decode errors, non-tolerable resource/invariant errors, and block splitting at/over capacity.

A black-box synthetic ET_EXEC CLI test invokes `analyze-elf` both with and without `--discover`, sets `--max-instructions 1`, and checks nonzero exit, the existing actionable `--max-instructions` hint, and `cfg.discover`/`analysis` failure attribution in session diagnostics. Synthetic ELF bytes are constructed by the test; no retail artifacts are used.

Full quality-gate results are recorded with the implementation handoff. These tests establish bounded-analysis behavior only; they do not establish executable coverage or title compatibility.

## Consequences and limitations

Speculative code guesses remain lenient only for the classified structural decode errors. Reaching a configured work bound now fails the full analysis with the existing typed error and CLI hint, rather than silently returning incomplete totals. Future `AnalysisError` variants require explicit classification at compile time. The counters remain local to a failed analysis call; this change does not promise rollback of internal transient work on global fatal errors.
