# REPS2 autonomous engineering state

Updated: 2026-11-05

## Current objective

Advance M4 real-title native execution. The EE `FindAddress` (0x83) HLE host service
is implemented and merged to `main` (PR #2, commit `8b7cede`): an honest service
that returns `0` = not found, never faked success, through the shared sorted
`kEeSyscallServices` dispatch table with registered guest-handler slots taking
precedence (ADR 0069). The Downhill Domination bounded probe now progresses past
PC `0x0025EF7C` (FindAddress handled) and stops at an unrelated unresolved
indirect-control-flow CFG gap at `pc=0x0025EF38`. Next step: investigate that
CFG gap to resume execution toward a repeatable guest-visible boot stage.

## Verified repository state

- Branch: `main` at HEAD `b966a96` (`docs: record FindAddress probe evidence and M4 unblocked`). CI green on Windows, Linux, and MSRV 1.88.
- Merged work on `main`:
  - PR #1 — EE guest syscall handlers via SetSyscall dispatch (commit `253ba31`, ADR 0069).
  - PR #2 — EE FindAddress (0x83) HLE service (commit `8b7cede`, ADR 0068).
  - Placeholder `path/to/filename.js` removed (commit `627c71f`).
- Open PRs: none.
- Active feature branches exist (e.g. `feat/m4-ee-findaddress-hle`, `feat/ee-syscall-hle-2026-11-04`, `feature/m4-native-execution-trace-2026-11-04`); none is "dirty" on `main`.

## Verified validation

- Rust formatting: passed (`cargo fmt --check`)
- Rust workspace tests: passed (271 tests, 0 failures)
- Rust clippy: passed (no warnings, `-D warnings`)
- Release native build: passed
- Full CTest: 30/30 passed (incl. `reps2_hle_syscall_codegen_smoke` 29 cases)

## M4 evidence status

### Downhill Domination (USA) — SCUS_971.77 — bounded probe re-validated with HLE on (2026-11-05)

- Boot ELF: `dh.elf` (temp fixture, not in repo); Input SHA-256 `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`
- Entry: `0x0010A008` (matches ELF entry)
- Analysis: 28,300 blocks; 146,425 instructions; 314 unsupported stops; 165 syscall/break traps
- HLE-on bounded probe (budget 5,000,000): `2,757,047` guest instructions executed natively
- Services observed: `syscalls=7`, `hle_handled=6` (6 HLE-hosted incl. FindAddress; 1 via registered guest handler through SetSyscall)
- `hle_pc=0x0025EF7C hle_number=0x83 hle_name=FindAddress hle_a0=0x80000000 hle_a1=0x80080000 hle_a2=0x0025EF38 hle_a3=0x0029E580` — matches the recorded call site
- FindAddress (0x83) **handled by HLE** (honest host service; returns 0 = not found since REPS2 populates no kernel table)
- Execution **progressed past the `0x0025EF7C` FindAddress boundary**; final stop `status=4` (`external_control_flow` / `unresolved_control_flow`) at `pc=0x0025EF38` — an unrelated CFG gap (indirect branch target not resolved), not the FindAddress boundary
- Native probe CTest: `reps2_generated_probe` passed
- No guest-visible boot stage demonstrated

### Black (USA) — SLUS_213.76 — native probe validated (2026-11-05)

- ISO SHA-256 `65768cade88bfbaf566b67a3ef7a0bda5a9669779833914ffe7cd73a3e45cccd`; boot ELF SHA-256 `141b05209570458af9c8e27ab1911f258da6353909d813cd572df026254ffe4c`
- Entry: `0x00100008` (matches ELF entry)
- Analysis: 101,956 blocks; 504,661 instructions; 1,068 unsupported stops; 243 syscall/break traps
- HLE-on bounded probe (budget 10,000,000): `290,344` guest instructions executed natively
- Services serviced in order: `0x3C` SetupThread, `0x3D` SetupHeap, `0x40` CreateSema (x2), `0x74` SetSyscall
- Previously stopped at unhandled `FindAddress` 0x83 at PC `0x0036D664`; same FindAddress contract as Downhill Domination applies. Black ELF not available locally for re-probe.
- `syscalls=7`, `hle_handled=6`, `sema_live=2`, `sema_id=1`
- No guest-visible boot stage demonstrated

### M4 boundary (both titles)

- `FindAddress` service 0x83 ABI: **RESOLVED** via independent behavioral reference (PS2Recomp `9e4fd79`, research only — no source copied). Contract: `$a0`=table start (inclusive), `$a1`=table end (exclusive), `$a2`=target; scans word entries in `[a0, a1)`, returns the guest address of the first matching word or `0` when none matches; KSEG0/KSEG1 aliasing (`addr & 0x1FFFFFFF`); memory-validated reads; unreadable entry ends the scan.
- Downhill Domination bounded probe re-run with HLE on: execution progresses past PC `0x0025EF7C` (FindAddress handled); stops at an unrelated `unresolved_control_flow` CFG gap at `pc=0x0025EF38`.
- Black ELF not available locally for re-probe; same FindAddress contract applies.
- Trustworthy local reference-execution oracle: not available.
- Repeatable guest-visible boot stage: not demonstrated.
- M4 boundary: **UNBLOCKED** — FindAddress no longer stops execution; remaining blocker is an unresolved indirect control flow target (CFG coverage), outside the FindAddress syscall scope.

## Active backlog

1. Investigate the unresolved indirect-control-flow CFG gap at `pc=0x0025EF38` (Downhill Domination) to resume execution past the FindAddress boundary.
2. Re-run bounded Downhill Domination and Black probes with HLE on after CFG coverage improves; target a repeatable guest-visible boot stage.
3. Audit syscall handler context preservation (`EeSyscallHandlerFrame` omits `sa` and `cop0`; confirm whether the modeled EE syscall path requires them).
4. Enforce branch protection and formal review on `main`; two recent PRs merged without formal review submissions.
5. Advance independent compiler/runtime workstreams not blocked by `FindAddress` (M4 is a priority, not the whole mission).

## Release-gate status

- Alpha: not claimed; end-to-end native execution exists, but supported guest-program scope and runtime coverage remain limited.
- Public Beta: not met; no three-title gameplay validation, installable desktop application, or sustained supported sessions.
- REPS2 1.0: not met.

This document records verified state, not intentions or compatibility claims.
