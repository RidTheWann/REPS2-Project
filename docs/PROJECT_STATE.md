# REPS2 autonomous engineering state

Updated: 2026-11-05

## Current objective

Advance M4 real-title native execution without inventing the unresolved EE `FindAddress` (0x83) ABI, while preserving the shared multi-game architecture. The M4 instruction-window tracing work has been delivered and pushed to PR #79; the next step is Black native probe validation, then shared runtime services required by observed traces.

## Verified repository state

- Branch: `feat/m4-syscall-boundary-diagnostics`
- HEAD: `2bf22b9` (8 commits ahead of origin; pushed, PR #79 updated)
- PR: `RidTheWann/REPS2-Project#79` (open; CI checks pending after push; merge state clean)
- Working tree: clean
- Latest delivered work: rolling bounded instruction-PC tracing with PC-before-trace ordering fix, trace regression assertions, FindAddress caller correlation, ADR 0068 extended with retail trace evidence, Black title added to compatibility matrix.

## Verified validation

- Rust formatting: passed
- Rust workspace tests: passed (270 tests, 0 failures)
- Rust clippy: passed (no warnings)
- Release native build: passed
- Full CTest: 30/30 passed
- `git diff --check`: passed

## M4 evidence status

- Real-title native execution evidence exists for Downhill Domination.
- Syscall boundary and bounded trace infrastructure are implemented.
- `FindAddress` service 0x83 ABI, return value, and memory effects: **UNKNOWN**.
- Trustworthy local reference-execution oracle: not available.
- Repeatable guest-visible boot stage: not demonstrated.
- M4: **INCOMPLETE**.

## Active backlog

1. Build and execute the generated Black native probe with a bounded budget; record the result without claiming boot. (Black: SLUS_213.76, 101,956 blocks; 504,661 instructions; CFG-aware C++ generated; native build/execution not yet tested.)
2. Preserve the caller correlation and trace regression assertions (delivered in PR #79); do not implement `FindAddress` without an oracle.
3. After M4 evidence is exhausted, prioritize shared runtime services required by observed traces, then second-title validation.
4. Remove `path/to/filename.js` placeholder from main (separate cleanup PR; does not block M4).

## Release-gate status

- Alpha: not claimed; end-to-end native execution exists, but supported guest-program scope and runtime coverage remain limited.
- Public Beta: not met; no three-title gameplay validation, installable desktop application, or sustained supported sessions.
- REPS2 1.0: not met.

This document records verified state, not intentions or compatibility claims.
