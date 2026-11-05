# REPS2 autonomous engineering state

Updated: 2026-11-05

## Current objective

Advance M4 real-title native execution toward a repeatable guest-visible boot stage.
The EE HLE layer now supports the observed early boot calls:
`SetupThread` (0x3C), `SetupHeap` (0x3D), `CreateSema` (0x40),
`FlushCache` (0x64), `SetSyscall` (0x74, including translated guest
handlers), and `FindAddress` (0x83). Unsupported calls remain explicit stops;
no success is fabricated.

The frontend CFG now performs bounded, within-basic-block constant propagation
for indirect `JR/JALR` source registers formed by `LUI`, `ORI`, `ADDIU`,
`DADDIU`, `ANDI`, or `XORI`. Resolved destinations are explicit CFG
`Jump`/`Call` edges; executable destinations are traversed, known
non-executable destinations are retained in `external_targets`, and unknown
register-derived destinations remain unresolved.

Pointer-table discovery now uses section-declared `SHF_EXECINSTR` ranges instead
of the program-header `PF_X` flag, so retail images whose single `PT_LOAD` is
writable and executable still yield stored code pointers (ADR 0070). With that
change the former Downhill Domination `0x0025EF38` boundary is translated and
executed; the current stop is budget exhaustion inside the guest's own
word-scan loop, not unresolved control flow.

## Verified repository state

- Default branch: `main` at `3409a10` (PR #4 squash-merged on 2026-11-05).
- PR #4 is **closed and merged**; merge commit
  `3409a10d09c9b21944107de16361fc3e2019f553` contains the M4 documentation,
  syscall-handler context preservation, indirect-target CFG analysis, and
  regression tests.
- PR #3 was **closed without a GitHub merge** after its documentation changes
  were folded into PR #4. Do not describe PR #3 as independently merged.
- `main` remains protected. Required checks are
  `Rust frontend (ubuntu-latest)`, `Rust frontend (windows-latest)`, and
  `Rust frontend (MSRV 1.88)`.
- PR #4's final head passed all three required checks on Ubuntu, Windows, and
  Rust MSRV 1.88 before squash merge. Review follow-up fixes were included in
  that verified head.

## Delivered in PR #4 (merged)

- Corrected stale project-state descriptions and removed the superseded
  FindAddress deferral text from ADR 0068.
- Updated README and CAPABILITIES to describe translated guest-handler dispatch
  through SetSyscall and the honest FindAddress HLE service.
- Preserved `SA` and the COP0 register array across guest syscall-handler entry
  and return; the handler's `$v0` remains the service result.
- Added a regression test that clobbers `SA` and COP0 inside a guest handler
  and checks they are restored.
- Added conservative static target resolution for same-block constant
  construction using `LUI/ORI/ADDIU/DADDIU/ANDI/XORI`.
- Added CFG fixtures for constant `JR` targets, a KSEG0 `JALR` target,
  `DADDIU` address construction, and a delay-slot write that must not alter
  the already-selected `JR` target.
- Preserve resolved indirect destinations as explicit `Jump`/`Call` CFG edges;
  known non-executable destinations remain visible in `external_targets`, and
  `JR $zero` is treated as a constant transfer to address zero.
- The merged source/test changes passed the required Windows, Ubuntu, and
  MSRV CI checks. Passing synthetic/host CI does not replace a real-title probe.

## M4 real-title evidence

### Downhill Domination (USA) — SCUS_971.77

Current bounded probe (2026-11-06, after ADR 0070 section-aware pointer discovery):

- Input: `Downhill Domination (USA).iso`, kept outside Git. The boot ELF was
  re-extracted with `reps2 extract-boot-elf`; the ISO was opened read-only and
  was not modified.
- Boot ELF SHA-256:
  `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`
  (identical to the recorded digest). Entry `0x0010A008`.
- Build: REPS2 `e4165c9` plus ADR 0070; `x86_64-pc-windows-msvc`; generated
  native module built with MSVC Release `/O2` as one dispatcher plus 8 parts.
- Analysis (`analyze-elf --discover`): 49,148 data-pointer words scanned (0
  before ADR 0070), 3,812 pointer roots, 8,400 accepted roots, 61,333 basic
  blocks, 314,561 decoded instructions, 1,798 unsupported stops, 629 unresolved
  indirect targets.
- `0x0025EF38` is now reported as `function: 0x0025EF38 (discovered)` and is
  translated. It is a frameless leaf routine held only in a handler-descriptor
  table at `0x00282218` (`(0x83 → 0x0025EF38)`, `(0x5A → 0x0025EF00)`), so
  neither `JAL` nor stack-prologue discovery could reach it.
- HLE-on native probe, instruction budget 5,000,000: `status=3`
  (`budget_exhausted`), `pc=0x0025EF5C`, `executed=5,000,000`, `syscalls=9`,
  `hle_handled=6`, `sema_live=2`.
- The former `unresolved_control_flow` stop at `0x0025EF38` is gone. Execution
  enters the guest's own word-scan routine and the final 16-PC window is the
  scan loop `0x0025EF50` → `0x0025EF54` → `0x0025EF5C`.
- Observed arguments at the last FindAddress boundary: `$a0=0x00000004`,
  `$a1=0x80080000`, `$a2=0x0025EF38`, so the loop walks roughly
  0x2002_0000 words. `$a0` provenance is the next investigation; the budget was
  deliberately not raised to hide it.
- No guest-visible boot stage was demonstrated.

Previous bounded probe, before the indirect-target resolver (historical):

- 28,300 blocks; 146,425 instructions; 314 unsupported stops; 165 syscall/break
  traps. Budget 5,000,000 yielded 2,757,047 executed guest instructions, seven
  syscalls, six handled. FindAddress at `0x0025EF7C` was handled, then the probe
  stopped with `unresolved_control_flow` at `pc=0x0025EF38`.

### Black (USA) — SLUS_213.76

Last recorded probe predates the FindAddress HLE implementation:

- ISO SHA-256:
  `65768cade88bfbaf566b67a3ef7a0bda5a9669779833914ffe7cd73a3e45cccd`.
- Boot ELF SHA-256:
  `141b05209570458af9c8e27ab1911f258da6353909d813cd572df026254ffe4c`.
- Entry: `0x00100008`; analysis: 101,956 blocks and 504,661 instructions.
- The old HLE-on probe reached FindAddress `0x83` at `0x0036D664`.
  The Black ELF was not available for a post-FindAddress re-probe.
- No guest-visible boot stage was demonstrated.

### Evidence limits

- The `0x0025EF38` boundary is resolved (ADR 0070): it is a discovered function
  root now translated and executed. Budget exhaustion occurs at `pc=0x0025EF5C`
  inside its own scan loop; that is not an unresolved-control-flow stop.
- A real-title re-probe requires the owner's legally obtained ISO/boot ELF in
  the local test environment. Game binaries and ISOs remain outside Git.
- An independent reference-execution oracle for the complete runtime path is
  not currently recorded.
- No Playable or multi-game compatibility claim is justified.

## Active backlog

1. **Investigate FindAddress argument `$a0` provenance**: the trace shows a guest
   call to the word-scan routine with `$a0=0x00000004` and budget exhaustion at
   `0x0025EF5C`. Confirm whether this reflects correct HLE service return values
   and proper register propagation after the previous syscall handler returned,
   and document any fix.
2. Inspect the target at `0x0025EF38` against the real ELF bytes and trace.
   Confirm it is an executable instruction address before treating CFG
   discovery as correct; investigate any new unresolved edge separately.
3. Re-run the Black probe with HLE enabled using its matching legal input
   digest; do not infer its progress from Downhill Domination.
4. Trace the caller's use of FindAddress `$v0` and verify that the
   not-found result is handled correctly by each title.
5. Continue runtime work only from observed guest paths; do not implement
   unrelated PS2 devices or claim compatibility from synthetic fixtures.
6. After one repeatable guest-visible boot stage, proceed to the next required
   service/device boundary and retain exact reproducibility evidence.
7. Establish the minimum multi-game proof using a second distinct title on
   the same shared compiler/runtime path.

## Release-gate status

- Alpha: not claimed; end-to-end native execution exists, but the supported
  real-title scope and runtime coverage remain limited.
- Public Beta: not met; no repeatable guest-visible boot stage, three-title
  gameplay validation, installable desktop application, or sustained
  supported sessions.
- REPS2 1.0: not met.

This document records verified state, not intentions or compatibility claims.
