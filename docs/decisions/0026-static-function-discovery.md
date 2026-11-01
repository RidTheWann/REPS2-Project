# ADR 0026: Static function-root discovery for stripped retail executables

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** `analyze-elf` / `recompile-elf` root discovery, CFG traversal provenance, and retail-executable measurement

## Context

Retail PS2 boot executables are commonly stripped: the extracted ELFs in the local
lawfully owned library contain **zero `STT_FUNC` symbols**. Before this increment,
`analyze-elf` therefore seeded the CFG from the ELF entry alone. On `Black (USA)`
that produced 9 basic blocks and 113 decoded instructions and then stopped at the
first `SYSCALL`, leaving virtually the whole executable unanalyzed. The same entry
point also seeds `recompile-elf`, so the AOT output could never cover real game
code.

An analyzer cannot recover function boundaries from symbols that do not exist, but
the instruction stream itself carries two strong, encoding-level entry signals:

1. a `JAL` whose target lands in file-backed executable bytes (a direct call);
2. a word that decodes as the canonical PS2/EE ABI prologue
   `addiu $sp, $sp, -N` with `N > 0` (a callee entry).

Both signals also occur in embedded data and in the middle of real functions, so
neither is a reachability proof. The design must add roots without turning the
analyzer into a guessing decompiler and without weakening the existing guarantee
that authoritative roots fail hard on undecodable input.

## Decision

### 1. Opt-in discovery

`analyze-elf` and `recompile-elf` accept `--discover`. Without the flag the
previous behavior is byte-for-byte preserved: seeds are still only the ELF entry
and executable `STT_FUNC` symbols. `--discover` adds `discover_function_roots`,
which scans every executable `PT_LOAD` segment in its declared alias view (so the
four EE aliases of one segment are not rescanned) and collects:

- `call_targets`: `JAL` targets that are themselves backed by file bytes in an
  executable segment;
- `prologue_candidates`: aligned words whose first instruction is
  `addiu $sp, $sp, -N`, `N < 0`.

A scan budget (`AnalysisLimits::max_function_roots`, default 500,000) stops the
scan deterministically and reports `truncated`; scanned words are counted for
provenance.

### 2. Candidates are not reachability claims

Discovered addresses are added as `EntryPoint`s labeled `discovered` and are
queued with **lenient** provenance. Authoritative seeds (ELF entry, function
symbols) are queued with **authoritative** provenance. The distinction is
propagated through the traversal:

- a block reached only from lenient seeds may fail to decode (trap in a delay
  slot, target entering a delay slot, unsplittable block) and is skipped and
  reported instead of aborting the analysis;
- a block reached from an authoritative seed fails hard, exactly as before;
- if authoritative reachability later arrives at an address that a lenient path
  had skipped, it is retried with hard-error semantics;
- if it arrives at an already decoded lenient block, that block's successors are
  re-queued authoritatively so lenient-only skips beneath it are retried.

Inactive/unknown roots are never promoted to `EntryPoint`s: `DiscoveryAnalysis`
reports `accepted_roots`, `skipped_roots` (candidates rejected by the traversal)
and `skipped_blocks` (all heuristic-only block starts that were skipped).

### 3. Delay-slot ownership

A heuristic leader that lands in a delay slot is not a boundary claim. The
authoritative control instruction still decodes, and the candidate is reported as
a delay-slot interior when its own queue entry is visited. A **trap**
(`SYSCALL`/`BREAK`/`ERET`) in *any* delay slot is rejected as an unsupported
delay slot: scalar/control IR cannot represent it, so admitting it would build a
graph that cannot be lowered.

### 4. Determinism

Candidates are `BTreeSet`s, roots are processed in address order, and the two
evidence sets are reported separately. Two runs over the same input produce the
same block/instruction counts.

## Measured result

Eight extracted retail ELFs, `analyze-elf --discover`, current decoder:

| image | candidates | accepted roots | skipped candidates | skipped heuristic blocks | basic blocks | decoded instructions | unsupported blocks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Black (USA) | 10,457 | 10,394 | 64 | 221 | 97,678 | 473,292 | 1,471 |
| Bully (USA) | 17,559 | 17,545 | 15 | 126 | 209,308 | 938,722 | 1,309 |
| Downhill Domination (USA) | 5,356 | 5,329 | 28 | 164 | 53,800 | 274,227 | 1,045 |
| God of War II (USA) | 6,333 | 6,265 | 69 | 308 | 66,143 | 318,630 | 1,281 |
| GTA San Andreas (USA v3.00) | 10,265 | 10,230 | 36 | 111 | 213,942 | 1,040,666 | 416 |
| GTA San Andreas (Indonesian v6) | 10,265 | 10,230 | 36 | 111 | 213,942 | 1,040,666 | 416 |
| Need for Speed Most Wanted (USA v2.00) | 16,977 | 16,918 | 60 | 408 | 178,511 | 841,886 | 1,102 |
| Resident Evil 4 (USA) | 7,763 | 7,729 | 35 | 217 | 74,864 | 364,607 | 912 |

Baseline for Black without `--discover`: 9 blocks, 113 instructions. All eight
images complete without truncation or hard analysis failure. The localized GTA
San Andreas image is the same boot executable as the US release, so it is listed
for completeness rather than as an independent data point.

`recompile-elf --discover` on Black completes in ~7 s and emits 97,678 blocks /
472,072 represented guest instructions / 1,471 explicit unsupported stops plus a
32 MiB RAM image. The generated C++ is ~178 MB, which makes translation-unit
scalability the next engineering problem; it is not evidence that the discovered
blocks are semantically correct or that a title boots.

## Consequences

- Stripped retail executables now yield a large, deterministic CFG instead of an
  entry-only stub, without changing default analysis behavior.
- Discovered code is explicitly labeled and reported; skipped candidates and
  skipped heuristic blocks expose the heuristic's failure surface instead of
  hiding it.
- Embedded data can still decode as plausible code. Accepted candidates are
  candidates, not validated functions; reachability from a seed is not proof of
  runtime reachability.
- Large single translation units are not viable at retail scale. Splitting the
  emitted CFG into multiple translation units (and linking them against the same
  runtime ABI) is required before native builds of discovered games are practical.
- No ISO, ELF, BIOS, or extracted game bytes are committed; the measurements are
  produced from lawfully owned local images outside the repository.

## Validation

- `discovers_jal_targets_and_stack_prologues`: both evidence sets, alias-view
  scan, non-executable `JAL` target rejection, deterministic counts.
- `stops_discovery_scan_at_the_candidate_limit`: budget truncation is reported.
- `tolerates_a_heuristic_root_inside_a_delay_slot`: an authoritative branch still
  decodes when a candidate claims its delay slot; the candidate is skipped.
- `rejects_a_trap_instruction_in_a_delay_slot`: trap delay slots are explicit
  unsupported stops.
- `discovers_roots_in_a_stripped_executable_image`: end-to-end synthetic ELF with
  no symbols reaches the discovered prologues and `JAL` callee.
- Existing authoritative-root rejection tests continue to pass unchanged.
- `cargo test --workspace` (140 tests), `cargo clippy -D warnings`, and the
  13-test CMake/CTest native suite pass.