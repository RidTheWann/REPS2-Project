# REPS2 Desktop Beta Readiness

Status: planning baseline updated 2026-11-11. This document is a Desktop product
gate; it does not replace `docs/RELEASE_GATES.md` or any engine milestone.

## 1. Release distinction

There are two separate claims:

- **Desktop Beta** means the Windows application is reliable, recoverable,
  understandable, accessible, installable, and supportable for its declared
  developer/early-adopter workflows.
- **REPS2 Public Beta Candidate** additionally requires at least three distinct
  PS2 titles to pass documented gameplay acceptance plans
  (`docs/RELEASE_GATES.md`). The application must never disguise an engine
  blocker as a desktop capability.

The Desktop Beta may expose import, inspection, native preparation, bounded
execution diagnostics, and evidence review while gameplay remains unavailable.
The word "Playable" must remain tied to actual compatibility evidence.

## 2. Existing foundation on main

The current Desktop Alpha already provides:

- Native WinUI 3 / .NET 10 Windows x64 shell and per-user portable installation.
- One-ISO import through the Rust frontend, managed library records, game detail,
  and evidence-derived lifecycle labels.
- Native project preparation, CMake/MSVC build orchestration, persisted job
  records, and ISO/ELF/build-identity provenance checks.
- Bounded RG REPS2 ENGINE probes with retained run-report JSON.
- Developer CLI reports, local settings, SDK/toolchain discovery, and a
  checksum-verified updater with startup handshake and rollback helper.

These establish an application foundation, not a Beta release. The current
limitations remain documented in `desktop/README.md` and
`docs/desktop-alpha-implementation.md`.

## 3. Desktop Beta acceptance gates

A Desktop Beta is not ready until each applicable gate has reproducible evidence.

### A. Library and data lifecycle

- [ ] Import is idempotent and duplicate-disc feedback opens the existing entry.
- [ ] Search, lifecycle filters, sorting, and empty/no-match/error states are
      usable on small and large libraries.
- [ ] A moved ISO can be relinked only when its full SHA-256 and recorded size
      match the content-addressed entry; no other manifest evidence is changed.
- [ ] Source integrity can be explicitly re-verified without hashing every ISO
      during each library refresh.
- [ ] Any remove/repair action distinguishes managed derived data from the
      user's original ISO and requires clear confirmation.
- [ ] Damaged manifests, missing artifacts, inaccessible paths, and interrupted
      writes fail safely and retain enough information for recovery.

### B. Build workflow and jobs

- [ ] Import/build stages show only real process stages and real progress values.
- [ ] Build Queue shows useful job details, output paths, diagnostics, timestamps,
      exit status, and recovery guidance.
- [ ] A user can cancel a running native build; the complete child-process tree
      is terminated and the persisted job becomes Cancelled.
- [ ] Failed jobs can be retried safely without overwriting an existing native
      build output or reusing mismatched provenance.
- [ ] Long-running operations remain responsive and cannot be started twice for
      the same game unintentionally.

### C. Execution and diagnostics

- [ ] Bounded run reports are parsed by schema and rendered as structured
      outcomes, guest stop, PC, instruction count, syscall and configuration
      evidence.
- [ ] Invalid, truncated, and unknown report schemas are shown as diagnostic
      errors rather than success.
- [ ] Reports can be exported with an explicit privacy warning and a reviewable
      list of included local paths/metadata.
- [ ] No exit code, native module, synthetic test, or bounded process completion
      can promote a title to Boot-tested or Playable.

### D. Settings and recovery

- [ ] Settings round-trip across restart and validate every configured executable
      and directory before use.
- [ ] Missing SDK tools, CMake, MSVC, Windows SDK, and permissions have actionable
      messages and a direct path to the relevant setting.
- [ ] Default paths, custom library paths, Unicode paths, and offline operation
      are covered by tests.
- [ ] User data is preserved across app upgrades and updater rollback.

### E. Windows distribution and security

- [ ] Clean install, upgrade, failed-upgrade rollback, and uninstall are tested
      on clean Windows 11 and supported Windows 10 22H2 environments.
- [ ] The installed app runs independently of the source checkout and developer
      PATH.
- [ ] Release artifacts have publisher authentication (Authenticode/signing)
      in addition to transport/checksum integrity.
- [ ] The installer and updater validate package identity, paths, size limits,
      and rollback behavior.
- [ ] Native-module execution is clearly identified as trusted, unsandboxed code.

### F. Accessibility, input, and product quality

- [ ] Every workflow is keyboard-operable with visible focus and predictable
      navigation/back behavior.
- [ ] Narrator/NVDA labels, UI Automation coverage, high contrast, 200% scaling,
      and reduced-motion behavior are verified.
- [ ] Controller parity is tested before being advertised as supported.
- [ ] Crash/diagnostic handling is understandable, privacy-aware, and does not
      block offline library access.
- [ ] End-to-end Windows smoke tests exercise import, library recovery, build
      failure, cancellation, diagnostics, settings, and update rollback.

## 4. Current increment and next sequencing

The current Desktop-first change set is in PR #84 and is **not merged**:

- Evidence-aware Library filters, alphabetical sorting, expanded search, result
  counts, and clear-filter recovery.
- Source ISO relink on Game Detail, gated to Recovery-required entries caused by
  a missing/mismatched source path.
- Relink verifies content-addressed game ID, manifest SHA-256, exact replacement
  file SHA-256, and recorded size before atomically replacing only
  `source_iso.path`.
- Unit tests cover correct relink, different-content rejection, wrong-size
  rejection, and invalid game-ID/path rejection.

These changes still require the current PR's Windows Desktop CI to pass.

Recommended implementation sequence after this increment:

1. **P0 — Library integrity & recovery:** explicit ISO re-verification, improved
   error/recovery detail, and safe managed-data repair semantics.
2. **P1 — Job operations:** cancellation UI, job detail view, diagnostics
   navigation, and safe retry/output handling.
3. **P2 — Structured diagnostics:** schema-driven execution report viewer,
   report export, and privacy review.
4. **P3 — Windows release quality:** installer/upgrade/uninstall acceptance,
   signing, clean-machine testing, UI Automation and accessibility.
5. **P4 — Beta candidate:** end-to-end acceptance on supported Windows systems,
   release documentation, known-issues policy, and rollback-tested release.

## 5. Separate engine/public-beta gate

Desktop maturity does not satisfy the engine release gate. The public beta
candidate remains blocked until at least three distinct titles pass gameplay
acceptance plans and all other applicable requirements in
`docs/RELEASE_GATES.md`. Until then, the desktop UI must expose only the
capabilities supported by the current engine and must not add a general Play
action.
