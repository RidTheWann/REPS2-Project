# REPS2 autonomous engineering state

Updated: 2026-11-05

## Current objective

Advance M4 real-title native execution without inventing the unresolved EE `FindAddress` (0x83) ABI, while preserving the shared multi-game architecture.

## Verified repository state

- Branch: `feat/m4-syscall-boundary-diagnostics`
- HEAD: `9a3d412` (caller correlation documented; trace regression assertions added locally)
- PR: `RidTheWann/REPS2-Project#79` (open; CI checks green; merge state clean)
- Working tree: clean
- Latest delivered work: large retail probe support and rolling bounded instruction-PC tracing.

## Verified validation

- Rust formatting: passed
- Rust workspace tests: passed
- Release native build: passed
- Full CTest: 30/30 passed
- `git diff --check`: passed
- GitHub CI: MSRV, Linux, and Windows checks passed

## M4 evidence status

- Real-title native execution evidence exists for Downhill Domination.
- Syscall boundary and bounded trace infrastructure are implemented.
- `FindAddress` service 0x83 ABI, return value, and memory effects: **UNKNOWN**.
- Trustworthy local reference-execution oracle: not available.
- Repeatable guest-visible boot stage: not demonstrated.
- M4: **INCOMPLETE**.

## Active backlog

1. Build and execute the generated Black native probe with a bounded budget; record the result without claiming boot.
2. Preserve the caller correlation and trace regression assertions in the next PR update; do not implement `FindAddress` without an oracle.
3. After M4 evidence is exhausted, prioritize shared runtime services required by observed traces, then second-title validation.

## Release-gate status

- Alpha: not claimed; end-to-end native execution exists, but supported guest-program scope and runtime coverage remain limited.
- Public Beta: not met; no three-title gameplay validation, installable desktop application, or sustained supported sessions.
- REPS2 1.0: not met.

This document records verified state, not intentions or compatibility claims.
