# REPS2 Desktop Implementation Roadmap

Version 1.0 (product blueprint). Phases take the application from the current
reality (no GUI, no title Boot-tested, M4 exit gate open) to the M7 Windows
platform gate. Cross-references: `../ROADMAP.md` (M0..M7),
`../RELEASE_GATES.md`, `COMPATIBILITY_UX_MODEL.md`, `INFORMATION_ARCHITECTURE.md`
(S-IDs), `APPLICATION_ARCHITECTURE.md`, `ENGINE_INTEGRATION_CONTRACT.md`
(C1..C8 interactions). Classification: **[E]/[P]/[PROP]/[N]/[UNKNOWN]**.

## 0. Premise: what the runtime blocks

The GUI is a product surface over an engine that is not yet capable of booting a
title. Two facts dominate sequencing:

1. **M4 is open** (`../ROADMAP.md`). No title reaches Boot-tested; Downhill
   Domination USA's latest executed probe stops `budget_exhausted` in a
   `PollSema` (0x45) poll loop on semaphore id 5 at `pc=0x00246DAC` (ADR 0119 /
   PR #68); the frontier is the IOP/SIF RPC producer behind that semaphore plus
   a scheduler, and the post-PR-#69 exact-title probe re-run is pending.
2. **M5/M6 are not started as gates.** No VU/GS/audio/input/media services, no
   multi-title proof, no Playable title.

Therefore the GUI roadmap is **evidence-forward**: it first makes truthful state
visible, then makes builds run, then wires execution that will *usually stop*,
and only then adds settings/saves/packaging. The GUI never substitutes for a
runtime gate (`../PRODUCT_VISION.md` line 104).

## 1. Phase overview

| Phase | Theme | Engine-milestone dependency | M7 screen coverage |
|---|---|---|---|
| P0 | Foundation: library index + state derivation + shell prototype | none (reads M3/M4 artifacts) | S1, S2, S5, S6, S11 (partial) |
| P1 | Add-Game + build pipeline UI | M3 (native build) **[E]** | S3, S4, S7, S12 |
| P2 | Execution/session + diagnostics | **blocked by M4/M5** | S8, S9, S10 |
| P3 | Settings + saves + relink | **blocked by M5 (saves)** | S11, S12 |
| P4 | Packaging + release hardening | **blocked by M6 (playable target)** | all |

## 2. Phase 0 - Foundation

**Goal:** a truthful read-only library that renders exactly the states the
artifacts support, plus the service skeleton.

**Prerequisites**
- None blocking. The artifacts P0 reads already exist **[E]**: `game.json`
  (schema v1), `<stem>.manifest.json` (schema v3), identity JSON, run report,
  `reps2 dev` reports.

**Deliverables**
- `LibraryService.ListRecords()` **[PROP]** implemented as a defensive scan of
  `<library>/games/*/game.json` using the documented content-addressed layout.
- `CompatibilityStateManager.Derive()` **[PROP]** producing the tuple
  `(stage, evidence_refs[], blockers[], allowed_actions[], disabled_actions[], recovery[])`
  from `COMPATIBILITY_UX_MODEL.md` §1, with "Unknown" for absent artifacts.
- Action-availability matrix wired to S5/S6 (Play only in `Playable`, which no
  title reaches today).
- WinUI 3 shell prototype: top bar + left rail + grid + inspector; S1/S2/S5/S6
  rendered from the derived state `[PROP]`.
- Optional **[PROP]** CLI-side `reps2 library list --json` (owned by the
  frontend team) so GUI and CLI share one index path (`../PRODUCT_VISION.md`
  line 92). Until it exists, the directory scan stands.

**Dependencies on engine milestones**
- M3 artifacts only **[E]**. No runtime dependency.

**Exit criteria**
- The library renders Downhill Domination USA as **Native-built** and stops
  (never Playable); Black USA as **NOT TESTED**; empty state as S2.
- No screen shows a state that cannot be reproduced from artifacts
  (`COMPATIBILITY_UX_MODEL.md` §7).
- Controller and keyboard traverse the shell with one focus model
  (`INFORMATION_ARCHITECTURE.md` §1).

**Risks**
- **State optimism drift:** a developer adds a UI-only state. Mitigation: state
  is a pure function; a test asserts derived state for synthetic artifacts.
- **Library scan fragility:** per-game directories can be half-written. The CLI
  uses staging + atomic rename **[E]**, so only completed records are visible.

## 3. Phase 1 - Add-Game + build pipeline UI

**Goal:** add one ISO and drive translate + native build to a linked module.

**Prerequisites**
- P0 shell and services.
- `reps2 add-game` **[E]** and `recompile-iso`/`recompile-elf --native-build`
  **[E]**.
- A host `cmake` + MSVC toolchain available to the GUI (the project already
  builds generated projects in CI, `../CLI.md`).

**Deliverables**
- `IsoIngestionService.ImportGame()` (**C1**) with the S3 dialog and duplicate/
  error states.
- `BuildJobManager.StartBuild()` (**C2** then **C3**) with per-stage progress on
  S4 derived from `pipeline.stage.*` events (**C6**); S7 Build Queue over a new
  app-owned job store under `Builds\` **[PROP]**.
- `FrontendClient` as the single `reps2.exe` spawn point **[PROP]**; exit-code
  mapping (**C8**).
- S12 Error & Recovery for import/build failures: surface the failing stage id and
  `REPS2_PIPELINE_STAGE_FAILED`/`REPS2_COMMAND_FAILED`, not raw log text.

**Dependencies on engine milestones**
- M3 native build path **[E]**. M4 is **not** required to reach Native-built, so
  P1 is deliverable while M4 is open.

**Exit criteria**
- A user adds one ISO with only its path; no manual BOOT.ELF or developer command.
- The build job reaches **Native-built** and displays `cache_key_sha256` and the
  target/ABI from the identity JSON.
- A failed compile shows the compiler diagnostics and allows retry/export of the
  generated project.
- No stage is promoted past its evidence (`COMPATIBILITY_UX_MODEL.md` §5).

**Risks**
- **Long builds:** Black/USA-scale translation is large (`../ROADMAP.md` M3:
  ~178 MB single unit, ~149 s). Mitigation: streaming progress, cancellation,
  bounded job pool.
- **Toolchain discovery:** the GUI must locate `cmake`/MSVC. **[UNKNOWN]** exact
  bundling; likely ship the builder or the SDK toolchain in P4.

## 4. Phase 2 - Execution/session + diagnostics

**Goal:** run a bounded session and report the exact stop, honestly.

**Prerequisites**
- P1 (a linked module exists).
- `rg-reps2-engine` runner **[E]** with `--ram/--media/--budget/--max-batches/`
  `--hle-syscalls/--instruction-trace` (**C4**).
- `reps2 dev` reports **[E]** (**C5**).

**Deliverables**
- `EngineSessionManager.StartSession/StopSession/StreamRunReport` **[PROP]** over
  **C4**. Stop is process termination (no in-process control) **[N]**.
- S8 Execution/Session: run-report view (outcome, `phase`, `pc`, `pc_hex`,
  `instructions_executed`, `syscalls_seen/handled`, per-batch guest status,
  optional trace PCs).
- S9 Developer Center indexing `reps2 dev sessions|inspect|summary`; S10
  Execution report with **Export**.
- `CompatibilityStateManager` gains the Boot-tested derivation rule (repeatable,
  non-`budget_exhausted` guest-visible stage) but will not fire yet.

**Dependencies on engine milestones**
- **Blocked by M4** for any Boot-tested claim; **blocked by M5** for reaching a
  visible stage. Until then, P2 ships the *runner client* and the honest
  `EXECUTION_BLOCKED` presentation, not a Play button.
- The thread scheduler that would let `WaitSema` resume is open M4/M5 work
  (`../ROADMAP.md`).

**Exit criteria**
- Running the current Downhill module yields a run report the GUI renders with the
  exact guest stop (`budget_exhausted` at `0x00246DAC`, `PollSema` poll loop on
  sema id 5) and stop PC; **Play is not offered**.
- Every `rg.reps2.engine.error.v1` code maps to a recovery view; exit 0 is never
  shown as "booted".
- Terminating a session leaves no orphan child and no pinned module in the UI.

**Risks**
- **False progress:** a glance at "0 exit code" implies success. Mitigation:
  presentation keys exclusively on `outcome` + guest status.
- **Blocked-feature limbo:** P2 cannot demo a boot. Accepted; it is a runtime gate,
  not a UI gate.

## 5. Phase 3 - Settings, saves, relink

**Goal:** persist user intent and recover from moved media.

**Prerequisites**
- P0-P2.
- **Blocked by M5** for save/memory-card storage (no runtime save format exists;
  SaveStore is **[UNKNOWN]**).

**Deliverables**
- `SettingsStore` **[N -> PROP]**: app config under `Config\` (library root
  override, runtime defaults, input mapping, logging, dev mode).
- `SaveStore` **[N]** once M5 defines a save format; until then S11 shows save
  paths as not-yet-supported.
- Relink flow on S12: rebind a moved source ISO **only** to the same digest
  (**C1** idempotent import; `COMPATIBILITY_UX_MODEL.md` §3 "Recovery required").
- Input mapping surfaced to the shell (controller-first), stored locally.

**Dependencies on engine milestones**
- **M5** for saves; **M5** media/input services for anything beyond a path.
- Relink needs only **C1** **[E]** and is deliverable earlier if desired.

**Exit criteria**
- Settings round-trip across restarts; invalid settings surface inline.
- Relink restores the previous stable state only when the digest matches; a
  different disc is refused.
- Save paths render as unsupported (not fabricated) until M5 lands.

**Risks**
- **Silent rebind:** binding a record to a different ISO. Mitigation: digest check
  is mandatory (`../PRODUCT_VISION.md` line 71).

## 6. Phase 4 - Packaging and release hardening

**Goal:** a one-ISO Windows application, controller-first, installable.

**Prerequisites**
- P0-P3.
- **Blocked by M6**: at least two distinct titles Boot-tested through the shared
  path; **blocked by M7** gate: launch supported modules through the shared runtime
  (`../ROADMAP.md` M7 exit gate).

**Deliverables**
- Windows App SDK packaging: single installer/package, MSVC runtime, staged
  `bin/reps2.exe` + `bin/rg-reps2-engine.exe` + SDK headers/libs
  (`../CLI.md` install tree), and the GUI.
- CI lane for the .NET/WinUI toolchain (the decision's cost,
  `APPLICATION_ARCHITECTURE.md` §10).
- Clean-install testing on a machine without the dev toolchain; Unicode-path
  testing (already exercised by CI for the runner, `../CLI.md`).
- Controller-first sign-off and accessibility pass (UIA, `Windows.Gaming.Input`).
- Diagnostics export with the privacy warning; crash report capture.

**Dependencies on engine milestones**
- **M6/M7** for a truthful "Playable". `../RELEASE_GATES.md`: public beta requires
  an installable Windows x64 app with one-ISO import and **at least three** titles
  validated through gameplay plans.

**Exit criteria**
- A user adds a game with only its ISO and sees truthful lifecycle/compatibility
  states; launch is offered only where M6 evidence supports it.
- The installed app runs with `PATH` isolated from the dev tree.
- No claim exceeds `../RELEASE_GATES.md` and `../COMPATIBILITY_MATRIX.md`.

**Risks**
- **Packaging the builder:** users may not have MSVC/CMake. **[UNKNOWN]** whether a
  bundled toolchain or a prebuilt-module distribution is shipped; this is the
  largest open packaging question.
- **Gate inflation:** an attractive UI masking an un-bootable engine. Mitigation:
  no Playable without a gameplay plan (`../PRODUCT_VISION.md` line 84).

## 7. Cross-phase risks

| Risk | Phase | Mitigation |
|---|---|---|
| UI-only state not reproducible from artifacts | P0 | Derivation is pure; synthetic-artifact tests assert every state |
| Exit code read as success | P1/P2 | Key presentation on JSON `outcome`/`status`; C8 documented |
| Scraping human CLI text | P1/P2 | FrontendClient/DiagnosticsService parse JSON only (**C5**) |
| Losing the no-FFI boundary | P0-P4 | GUI never links engine/runtime headers; subprocess only |
| Orphan children / pinned modules | P2 | One session process; kill on Stop and on app exit |
| Copyrighted data in library/Git | all | GUI writes no game data into Git; ISO referenced by path |

## 8. Screen and gate coverage

| Screens | Phase | Interaction used |
|---|---|---|
| S1, S2, S5, S6 | P0 | C7 |
| S3, S4, S7, S12 | P1 | C1, C2, C3, C6, C8 |
| S8, S9, S10 | P2 | C4, C5 |
| S11 | P0 (partial), P3 | new stores |
| S13 command palette | P1 | over C1..C6 |

Release-gate mapping: Phase 0-1 contribute to **Alpha**; Phase 2-3 and M6 evidence
are required for **Public beta candidate**; `../RELEASE_GATES.md` **1.0** needs the
full multi-game matrix, packaging, security review, and independent audit.

## 9. Explicitly blocked / not-started

- **Boot-tested presentation:** blocked by M4 (IOP/SIF RPC handshake, scheduler)
  and M5 (INTC/CPU interrupt delivery, VU/GS/audio/input).
- **Playable / Play button:** blocked by M6 gameplay acceptance plans; **no
  producer exists today** **[N]**.
- **Save/memory-card:** blocked by M5 **[UNKNOWN]** format.
- **In-process session control (pause/step/stop):** **[N]**; Stop = kill child.
- **Signed modules / trusted-module policy:** deferred (`../ROADMAP.md`).

## 10. Out of scope

- No recompiler, decoder, or engine changes from the GUI track ("one cohesive
  vertical slice per PR", `AGENTS.md`).
- No new FFI, no per-instruction callbacks.
- No network/metadata services; the library works offline
  (`INFORMATION_ARCHITECTURE.md` §5).
- No architectural-substitution claim: the launcher is the final product surface,
  not the engine (`../PRODUCT_VISION.md` line 51).
