# REPS2 autonomous engineering state

Updated: 2026-11-06

## Current objective

Advance M4 real-title native execution toward a repeatable guest-visible boot stage.
The EE HLE layer now supports the observed early boot calls:
`SetupThread` (0x3C), `SetupHeap` (0x3D), `CreateThread` (0x20, dormant host
records only), `CreateSema` (0x40), `GetEntryAddress` (0x5B), `FlushCache`
(0x64), `SetSyscall` (0x74, including translated guest handlers and the
guest-visible 512-slot table at `0x80011F80`), and `FindAddress` (0x83). The
documented Timer 3 mode word (`0x10001810`) is modelled without counting or
interrupts, and the frontend can analyse runtime-copied code declared with
`--relocated-code`. Unsupported calls remain explicit stops; no success is
fabricated.

With these changes Downhill Domination completes the ps2sdk `InitAlarm` kernel
patch, executes the copied alarm blob at `0x80076000`, obtains a thread id from
`CreateThread` (0x20), and stops at `StartThread` (0x22) — the next
unimplemented service. That is real guest-visible progress, not a visible boot
stage.

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

- Default branch: `main` at `f3419d1` (PR #17 squash-merged on 2026-11-06).
- PR #17 publishes validated syscall handlers into the guest-visible table and
  executes the Downhill Domination `InitAlarm` patch path (ADRs 0072–0074).
  PR #16 added the bounded EE syscall-boundary sequence trace (ADR 0071).
  PRs #15 and earlier are historical.
- The `CreateThread` change described in this document is on a feature branch,
  not yet merged.
- `main` remains protected and requires strict, up-to-date checks:
  `Rust frontend (ubuntu-latest)`, `Rust frontend (windows-latest)`, and
  `Rust frontend (MSRV 1.88)`.

## Delivered in this change (branch, unmerged)

- `CreateThread` (0x20) reads the documented nine-word `ee_thread_t` parameter
  block through the normal EE memory path, validates `func`, `stack`,
  `stack_size`, `gp` and `initial_priority`, and returns the lowest free id from
  a bounded host record table (ids start at 1; thread 0 is the documented idle
  thread). Every refusal returns `-1`, allocates nothing and records an explicit
  reason (ADR 0075).
- The parameter block is input only: no guest memory is written, the caller's
  uninitialized `status` word is never interpreted, and no thread status,
  stack image, argv block, ready-queue entry or scheduler state is created.
- `reps2_hle_syscall_codegen_smoke` grew from 35 to 39 cases: a valid call
  returns id 1, every invalid parameter class is refused, the table bound is
  enforced, and the generated dispatcher routes service 0x20.
- The real-title probe now services `CreateThread` at `pc=0x0025A4E4` and stops
  at `StartThread` (0x22) at `pc=0x0025A504` with `$a0=0x00000001` (the returned
  id) and `$a1=0x00761208` (the argv block). This is one observed service
  further along the same guest path, not a thread-scheduling claim.

## Delivered in PR #17 (merged)

- `SetSyscall` (0x74) now publishes the validated handler pointer into the
  guest-visible syscall table at `0x80011F80` (4-byte entries indexed by service
  number), in addition to the host registration. The table covers 512 slots so
  the documented extended services used by client kernel patches are accepted;
  registration is refused if the computed slot is not writable (ADRs 0072 and
  0073).
- `GetEntryAddress` (0x5B) returns `0x80011F80 + number * 4` with 32-bit modular
  arithmetic, matching ps2sdk's documented patch arithmetic (ADR 0073).
- EE Timer 3 mode (`0x10001810`) is modelled as the documented 32-bit mode word:
  reset 0, only the writable mode bits retained, no counting or interrupts
  (ADR 0073).
- `recompile-elf --relocated-code DEST:SOURCE:SIZE` admits runtime-copied guest
  code as an authoritative CFG root; the initial RAM image is unchanged and the
  manifest (version 3) records each region (ADR 0074).
- `reps2_hle_syscall_codegen_smoke` grew from 31 to 35 cases; Rust unit tests
  cover relocated-segment decoding and the manifest version bump.
- The Downhill Domination probe completes the ps2sdk `InitAlarm` patch and stops
  at the then-unimplemented `CreateThread` (0x20) instead of budget-exhausting
  in a scan loop.

## Delivered in PR #13 (merged)

- Added `executable_section_ranges`, derived from allocated `SHF_EXECINSTR`
  sections, so discovery works on retail images whose only `PT_LOAD` is
  writable and executable (ADR 0070).
- Applied those ranges symmetrically: code words are decoded as code, non-code
  words are read as pointers, and only section-executable addresses are
  admitted as candidates. Falls back to the `PT_LOAD` `PF_X` rule when the
  section table is absent or does not intersect the segments.
- Added a regression fixture with one RWX `PT_LOAD`, split section permissions,
  a pointer-shaped code word, and a `jal` encoding in `.data`.
- Re-ran the Downhill Domination analysis and native probe; the former
  `0x0025EF38` unresolved-control-flow stop is gone.
- Passing host CI does not replace a real-title probe, and the probe is still a
  bounded budget test, not a guest-visible stage.

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

Current bounded probe (2026-11-06, branch at ADRs 0072–0075):

- Input: `Downhill Domination (USA).iso`, kept outside Git. Boot ELF extracted
  with `reps2 extract-boot-elf`; ISO opened read-only, not modified. Boot ELF
  SHA-256 `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`
  (matches the recorded digest), entry `0x0010A008`.
- Frontend rebuilt from the branch; generated probe built with MSVC Release
  `/O2`, one dispatcher plus 8 parts, C++20.
- Analysis:
  `reps2 recompile-elf dd.elf dd.cpp --discover --tu-count 8 --native-build
  --probe-budget 5000000 --hle-syscalls --relocated-code 80076000:2829F8:740`
  reported 7,152 candidates, 7,145 accepted roots (7,144 ELF + 1 relocated),
  59,927 basic blocks, 309,425 instructions, 715 unsupported stops, 255 guest
  trap stops, 612 unresolved indirect targets, 0 external targets, and one
  relocated region `0x80076000 <- 0x002829F8 (0x740 bytes)`.
- Probe result: `status=14` (`unhandled_syscall`), `pc=0x0025A504`,
  `executed=2,946,363`, `syscalls=29`, `hle_handled=18`,
  `hle_number=0x22` (`StartThread`), `hle_a0=0x00000001`,
  `hle_a1=0x00761208`, `hle_a2=0x00000400`, `hle_sp=0x0093C7B0`,
  `sema_calls=3`, `sema_live=3`.
- The sequence shows the full InitAlarm patch: `SetSyscall(0x5A, kCopy)`,
  copies to `0x80076000` (0x740 bytes) and `0x00082000` (0x28 bytes),
  `FlushCache` twice, `SetSyscall(0x5B, 0x80076000)`, then six
  `GetEntryAddress` calls served by the translated blob handler, each followed
  by `SetSyscall(slot, stub)`:
  0xFC→`0x80076440`, 0xFE→`0x80076440`, 0xFD→`0x800762A0`,
  0xFF→`0x800762A0`, 0x12C→`0x80076488`, 0x08→`0x800766C0`.
  A further `FlushCache` and a third `CreateSema` precede `CreateThread`.
- `CreateThread` is now serviced at `pc=0x0025A4E4` with the recorded
  `$a0=0x0093C7B0`, `$a1=0x002A40F0`, `$a2=0x00000400`, `$a3=0x0029E580`.
  The raw sequence record is
  `0x0025A4E4:32:H:0x0093C7B0:0x002A40F0:0x00000400:0x0029E580:0x0025B0D0`.
  The guest stores the returned id and immediately calls `StartThread` (0x22),
  recorded as
  `0x0025A504:34:U:0x00000001:0x00761208:0x00000400:0x0029E580:0x00760000`:
  `$a0=1` is the id REPS2 returned and `$a1=0x00761208` is the argv block.
- The translated blob contains its own six-entry `(number, handler)` table at
  `0x80076710`; the observed returns match entries in that table, which is only
  correct if the guest's `Copy` and the translated lookup share the same RAM
  image.
- No guest-visible boot stage is demonstrated yet. `CreateThread` only created a
  dormant host record; the next required service is `StartThread` (0x22), which
  needs a ready queue and a defined scheduler interaction.

Previous bounded probe (ADR 0070 baseline, historical):

- 7,144 accepted roots, 59,922 blocks, 309,411 instructions; the probe stopped
  as `unsupported_memory_access` at `pc=0x0025F34C` (Timer 3 mode read) after
  completing the `0x0025EF90` handler-table recovery.

Earlier bounded probe (before the syscall table, historical):

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

- The former `0x0025EF5C` budget-exhaustion stop and the `0x0025F34C` Timer 3
  read stop are both resolved on the branch. The current stop is
  `unhandled_syscall` for `CreateThread` (0x20), which is a genuine
  unimplemented service, not an unresolved control flow or memory gap.
- The relocated alarm blob is developer-declared input (`--relocated-code`);
  its translation is not automatic inference. Only the entry function reachable
  from the declared root is translated.
- A real-title re-probe requires the owner's legally obtained ISO/boot ELF in
  the local test environment. Game binaries and ISOs remain outside Git.
- An independent reference-execution oracle for the complete runtime path is
  not currently recorded.
- No Playable or multi-game compatibility claim is justified.

## Active backlog

1. **Implement `StartThread` (0x22)**: the current stop. The observed call is
   `StartThread(thid=1, args=0x00761208)` where `1` is the id REPS2 returned
   from `CreateThread`. A faithful implementation needs at least a ready-state
   transition, a priority-ordered ready queue, stored argv context, and an
   explicit decision about scheduler interaction (the guest assumes the new
   thread can later be dispatched). Do not claim the thread runs until a
   dispatcher executes translated guest code for it.
2. Extend relocated-code coverage only as execution reaches it: the alarm blob's
   other functions (0x80076038 onward) and the `eenull` stub at `0x00082000`
   are not yet translated; declare them when an observed call needs them.
3. Decide whether HLE dispatch should honour direct guest writes to the
   syscall-table slots. The table is real guest RAM, but dispatch currently
   consults only host registrations; no observed title has patched a slot
   directly yet.
4. Re-run the Black probe with HLE enabled using its matching legal input
   digest; do not infer its progress from Downhill Domination.
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
