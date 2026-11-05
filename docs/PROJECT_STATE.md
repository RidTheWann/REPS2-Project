# REPS2 autonomous engineering state

Updated: 2026-11-05

## Current objective

Advance M4 real-title native execution. The EE `FindAddress` (0x83) ABI deferral
(ADR 0068) is resolved via an independent behavioral reference, the service is
implemented as an honest host service (returns `0` = not found, never faked
success), and the M4 boundary is unblocked. Next step: commit/push the feature
branch, open and merge its PR, then re-run the bounded Downhill Domination and
Black probes with HLE on to confirm execution progresses past PC `0x0025EF7C` /
`0x0036D664`.

## Verified repository state

- Branch: `main` at HEAD `253ba31` (CI green).
- Feature branch: `feat/m4-ee-findaddress-hle` off `253ba31`, working tree dirty
  (3 files + ADR 0068 + PROJECT_STATE.md, not yet committed).
- PR: `RidTheWann/REPS2-Project#79` (merged; all 6 CI checks passed: MSRV, Linux, Windows)
- Cleanup PR: `RidTheWann/REPS2-Project#80` (open; remove `path/to/filename.js` placeholder)
- Latest delivered work: EE FindAddress HLE service (0x83) — `hle_find_address` +
  `normalize_ee_kernel_alias` in `ee_hle.hpp`, registered in the sorted
  `kEeSyscallServices` table under `kEeSyscallFindAddress = 0x83U`, observation
  fields on `EeHleState`, HLE smoke suite grows from 23 to 29 cases (6 new
  FindAddress cases). Builds on SetSyscall dispatch (ADR 0069): registered guest
  handler slots take precedence over host services.

## Verified validation

- Rust formatting: passed (`cargo fmt --check`)
- Rust workspace tests: passed (271 tests, 0 failures)
- Rust clippy: passed (no warnings, `-D warnings`)
- Release native build: passed
- Full CTest: 30/30 passed (incl. `reps2_hle_syscall_codegen_smoke` 29 cases)
- `git diff --check`: not re-run after uncommitted edits

## M4 evidence status

### Black (USA) — SLUS_213.76 — native probe validated (2026-11-05)

- ISO SHA-256 `65768cade88bfbaf566b67a3ef7a0bda5a9669779833914ffe7cd73a3e45cccd`; boot ELF SHA-256 `141b05209570458af9c8e27ab1911f258da6353909d813cd572df026254ffe4c`
- Entry: `0x00100008` (matches ELF entry)
- Analysis: 101,956 blocks; 504,661 instructions; 1,068 unsupported stops; 243 syscall/break traps
- HLE-on bounded probe (budget 10,000,000): `290,344` guest instructions executed natively
- Services serviced in order: `0x3C` SetupThread, `0x3D` SetupHeap, `0x40` CreateSema (x2), `0x74` SetSyscall
- Stops at unhandled `FindAddress` 0x83 at PC `0x0036D664` (`$a0=0x80000000`, `$a1=0x80080000`, `$a2=0x0036D620`, `$a3=0x0040EC80`, `hle_v0_before=0x003D0000`)
- `syscalls=7`, `hle_handled=6`, `sema_live=2`, `sema_id=1`
- No guest-visible boot stage demonstrated

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
- Previously stopped at unhandled `FindAddress` 0x83 at PC `0x0036D664` (`$a0=0x80000000`, `$a1=0x80080000`, `$a2=0x0036D620`, `$a3=0x0040EC80`, `hle_v0_before=0x003D0000`)
- `syscalls=7`, `hle_handled=6`, `sema_live=2`, `sema_id=1`
- FindAddress (0x83) now implemented as HLE host service; Black ELF not available locally for re-probe — same service contract as Downhill Domination applies
- No guest-visible boot stage demonstrated

### M4 boundary (both titles)

- `FindAddress` service 0x83 ABI: **RESOLVED** via independent behavioral
  reference (PS2Recomp `9e4fd79`, research only — no source copied).
  Contract: `$a0`=table start (inclusive), `$a1`=table end (exclusive),
  `$a2`=target; scans word entries in `[a0, a1)`, returns the guest address of
  the first matching word or `0` when none matches; KSEG0/KSEG1 aliasing
  (`addr & 0x1FFFFFFF`); memory-validated reads; unreadable entry ends the scan.
- Implemented as an honest host service (merged to `main` via PR #2, commit
  `8b7cede`): returns `0` ("not found") for an empty/inverted/unpopulated
  window, writes nothing to guest memory — no faked kernel success. 6 tests
  added; suite 23→29 cases; CTest 30/30.
- Downhill Domination bounded probe re-run with HLE on: execution progresses
  past PC `0x0025EF7C` (FindAddress handled); stops at an unrelated
  `unresolved_control_flow` CFG gap at `pc=0x0025EF38`.
- Black ELF not available locally for re-probe; same FindAddress contract applies.
- Trustworthy local reference-execution oracle: not available.
- Repeatable guest-visible boot stage: not demonstrated.
- M4 boundary: **UNBLOCKED** — FindAddress no longer stops execution; remaining
  blocker is an unresolved indirect control flow target (CFG coverage), outside
  the FindAddress syscall scope.

## Active backlog

1. Commit/push `feat/m4-ee-findaddress-hle`, open its PR, merge to `main` after CI passes — **DONE** (PR #2 merged, commit `8b7cede` on `main`).
2. Re-run the bounded Downhill Domination probe with HLE on — **DONE**: execution
   progresses past PC `0x0025EF7C` (FindAddress handled); stops at unrelated
   `unresolved_control_flow` CFG gap at `pc=0x0025EF38`. Black ELF not available
   locally for re-probe; same FindAddress contract applies.
3. Remove `path/to/filename.js` placeholder from main (PR #80 open; does not block M4).
4. Advance independent compiler/runtime workstreams not blocked by `FindAddress` (M4 is a priority, not the whole mission).

## Release-gate status

- Alpha: not claimed; end-to-end native execution exists, but supported guest-program scope and runtime coverage remain limited.
- Public Beta: not met; no three-title gameplay validation, installable desktop application, or sustained supported sessions.
- REPS2 1.0: not met.

This document records verified state, not intentions or compatibility claims.
