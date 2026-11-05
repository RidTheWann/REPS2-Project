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
`DADDIU`, `ANDI`, or `XORI`. Targets are queued only when the calculated
32-bit address belongs to an executable view. This is static discovery, not a
claim that a retail game has passed the former `0x0025EF38` boundary: a new
Downhill Domination probe with this resolver has not yet been run.

## Verified repository state

- Default branch: `main` at `b966a96` (last verified main commit during this work).
- PR #4 is the active integration branch, based on `main`; its changes include
  the documentation corrections originally proposed by PR #3, EE syscall
  context preservation, indirect-target analysis, and CFG regression tests.
- PR #3 was **closed without a GitHub merge** after its documentation changes
  were folded into PR #4. Do not describe PR #3 as independently merged.
- `main` has branch protection enabled. Required checks are
  `Rust frontend (ubuntu-latest)`, `Rust frontend (windows-latest)`, and
  `Rust frontend (MSRV 1.88)`.
- Do not merge PR #4 until the required checks for its final head commit pass
  and the diff has been reviewed.

## Delivered in the active PR #4

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
- The latest source/test changes must remain subject to the required Windows,
  Ubuntu, and MSRV CI checks. A passing synthetic/host CI run does not replace
  a real-title probe.

## M4 real-title evidence

### Downhill Domination (USA) — SCUS_971.77

Last verified bounded probe, before the indirect-target resolver was added:

- Input ELF SHA-256:
  `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`.
- Entry: `0x0010A008`.
- Analysis: 28,300 blocks, 146,425 instructions, 314 unsupported stops,
  and 165 syscall/break traps.
- HLE-on instruction budget: 5,000,000; native guest instructions executed:
  2,757,047.
- Seven syscalls observed; six handled (including FindAddress), and one
  dispatched through a registered guest handler.
- FindAddress at `0x0025EF7C` was handled. That probe then stopped with
  `unresolved_control_flow` at `pc=0x0025EF38`.
- No guest-visible boot stage was demonstrated.

### Black (USA) — SLUS_213.76

Last recorded probe predates the FindAddress HLE implementation:

- ISO SHA-256:
  `65768cade88bfbaf566b67a3ef7a0bda5a9669779833914ffe7cd73a3e45cccd`.
- Boot ELF SHA-256:
  `141b05209570458af9c8e27ab1911f258da6353909d813cd572df026254ffe4c`.
- Entry: `0x00100008); analysis: 101,956 blocks and 504,661 instructions.
- The old HLE-on probe reached FindAddress `0x83` at `0x0036D664`.
  The Black ELF was not available for a post-FindAddress re-probe.
- No guest-visible boot stage was demonstrated.

### Evidence limits

- The Downhill Domination stop at `0x0025EF38` is the previous probe result,
  not evidence that the new static resolver still stops there or now passes it.
- A real-title re-probe requires the owner's legally obtained ISO/boot ELF in
  the local test environment. Game binaries and ISOs remain outside Git.
- An independent reference-execution oracle for the complete runtime path is
  not currently recorded.
- No Playable or multi-game compatibility claim is justified.

## Active backlog

1. Run the bounded Downhill Domination probe with the new resolver and HLE.
   Record the exact input digest, REPS2 commit, command/configuration, resolved
   target, executed instruction count, and final stop/guest-visible stage.
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
