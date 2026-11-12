# REPS2 Screen Specifications

Per-screen behaviour for screens **S1–S13** (IDs, nav model, and terminology are
canonical in `INFORMATION_ARCHITECTURE.md`; lifecycle states and the action
matrix are canonical in `COMPATIBILITY_UX_MODEL.md`; tokens/components/motion are
canonical in `design-system/reps2/MASTER.md`). Where this file and the backbone
disagree, the backbone wins.

---
## 0. Conventions

- **Shell regions** used by every screen: **top command bar** (56px), **left nav
  rail** (240px, collapsible 64px), **content**, **right inspector** (360px,
  collapsible). Region rules: `MASTER.md` §4.
- **Artifact→field notation:** every displayed value is written as
  `<artifact>.<field>` and may only render if that artifact exists and parses.
  An absent/unparseable artifact is `Unknown` — never a default, never `0`,
  never a green state (`COMPATIBILITY_UX_MODEL.md` §1).
- **Status pill** is the only renderer for lifecycle: icon + label + status
  token + tooltip stating the evidence requirement (`MASTER.md` §8).
- **No fabricated percentages.** Pipeline nodes show pending/running/succeeded/
  failed/skipped only. A determinate bar renders **only** when a real fraction
  exists in the artifact; otherwise skeleton/indeterminate (`CLI.md`, UX model §7).
- **[PROTOTYPE-ONLY]** marks behaviour that is spec-defined here but has **no**
  backend today. Prototype-only data must be visually flagged in dev builds and
  must not be presented to a user as verified.
- **Never rendered:** "emulator", "ROM", scene art, copyrighted covers (covers
  are generated placeholders, `aria-hidden` decorative or title-text alt).

### 0.1 Artifact field legend (authoritative field names)

| Artifact (producer) | Fields referenced by these specs |
|---|---|
| `game.json` — library manifest, `schema_version:1` (`reps2 add-game`, `rust/frontend/src/library.rs`) | `game_id`, `display_name`, `serial_candidate`, `source_iso.path/.sha256/.size_bytes/.copied_into_library`, `disc.volume_identifier/.logical_block_size`, `boot.filename/.sha256/.size_bytes/.system_cnf_target`, `boot.elf.entry_point/.machine/.file_type/.loadable_segment_count/.symbol_count/.relocation_count`, `analysis.status/.error/.entry_point_count/.basic_block_count/.decoded_instruction_count/.static_edge_count/.unsupported_block_count/.unresolved_indirect_count/.external_target_count`, `native_build.playable/.playability_status/.translated_instruction_count/.explicit_unsupported_count/.generated_cpp/.generated_ram_image`, `artifacts.boot_elf/.generated_cpp/.initial_ee_ram` |
| `<stem>.manifest.json` — build manifest, `manifest_version:3` (`reps2 recompile-elf`, `rust/frontend/src/build_manifest.rs`) | `tool.name/.version/.host_target`, `input.path/.sha256/.size_bytes`, `guest.entry_point/.machine/.loadable_segment_count`, `analysis.limits.*`, `program.entry_point_count/.basic_block_count/.guest_instruction_count/.unsupported_stop_count/.guest_exception_count/.static_edge_count/.unresolved_indirect_target_count/.external_target_count`, `translation.translation_unit_count/.primary_output/.part_outputs[]`, `artifacts.initial_ee_ram/.ram_image_bytes`, `relocated_code[]`, `playable:false` |
| `reps2-native-build-identity-<config>.json` (generated CMake project) | `cache_key_sha256`, toolchain report (source-ELF digest, translated-source digest, RAM-image digest, runtime ABI/header digest, IR+manifest schema versions, compiler/linker digests, target, generator, config, compile/link flags) |
| **engine run report** — `rg.reps2.engine.run_report.v1` (`rg-reps2-engine`, `engine/src/main.cpp`) | `outcome`, `session.phase/.pc/.pc_hex/.instructions_executed/.run_batches/.elapsed_microseconds/.media_present/.syscalls_seen/.syscalls_handled`, `module.name/.target.architecture`, `input.ram_image/.media_image/.media_sectors`, `config.instruction_budget_per_batch/.max_batches/.hle_syscalls`, `runs[].batch/.guest_status/.guest_status_code/.budget_supplied/.instructions_before/.instructions_after/.instructions_this_run/.pc_before/.pc_after/.invoked`, `instruction_trace_pcs[]` |
| **native probe trace line** (generated probe, `rust/frontend/src/native_build.rs`) | `entry`, `status`, `stage`, `pc`, `budget`, `executed`, `cause`, `epc`, `cop0_status`, `badvaddr`, `exccode`, `syscall`, `hle`, `syscalls`, `hle_handled`, `hle_pc`, `hle_number`, `hle_name`, `sema_*`, `sif_*`, `syscall_seq_count`, `syscall_seq` |
| developer reports — `reps2 dev …` (`docs/schemas/developer-report-v1.schema.json`) | `kind` ∈ {`session_list`,`session_inspection`,`session_summary`,`session_comparison`,`capabilities`,`error`}, `schema_version:1`, `status`, `outcome`, `event_count`, `diagnostic_count`, `sessions[]`, `available_count`, `truncated`, `integrity_errors[]` |

> **Note (inconsistency):** COP0 readout (`cause`, `epc`, `cop0_status`,
> `badvaddr`, `exccode`) and `syscall_seq` are fields of the **generated native
> probe trace line**, **not** of the engine run report. The engine run report
> carries `outcome`/`session.phase`/`runs[].guest_status` instead. Screens below
> read COP0 values from the probe trace only. See the report at the end of this
> file.

---
## S1 — Library

**Purpose.** Browse, filter, and sort owned games with truthful lifecycle status;
entry point to Game Detail and (only when Playable) Play.

**Layout.**
- Top bar: `search` (filters visible titles), global status (active jobs count).
- Left rail: nav destinations; active item `Library`.
- Content: grid of **Game cards** (`MASTER.md` §7). Card = placeholder cover,
  `display_name`, `serial_candidate` (mono), status pill, primary action slot.
  Sort/filter toolbar above grid (region: title, serial, lifecycle, last-imported).
- Right inspector: selected card summary (identity + evidence refs + tooltip).
- Status bar: library count, engine ABI, active jobs (`PROP` queue counts).

**States.**
- *Loading:* skeleton grid (unknown duration; indeterminate).
- *Empty:* S2 (first-run onboarding — see S2).
- *Populated:* one card per `game.json` discovered under the library root.
- *Error:* library root unreadable → `role="alert"` banner + recovery; grid hidden.
- *Disabled:* n/a (browse always available). Individual card's Play slot handled
  per action matrix.

**Focus order (keyboard ≡ controller, `MASTER.md` §5/§9).** skip-to-content →
search → filter/sort controls → rail items → card grid (row-major), first card
first → inspector toggle → status bar. Card grid uses a single roving tab index;
arrows move within grid, `Enter`/`A` opens detail.

**Actions & enablement.**
| Action | Enabled when | Disabled reason shown |
|---|---|---|
| Open game | always | – |
| Play | card state = `Playable` | "Not playable — `<playability_status>`" (from `game.json.native_build.playability_status`) |
| Run test session | card state = `Boot-tested` (replaces Play) | – |
| Next valid action | any non-playable state | button label = next action per action matrix |
| Refresh library index [PROTOTYPE-ONLY: index API not implemented] | – | "Index rebuild not yet available" |

**Data fields → source.** title `game.json.display_name`; serial
`game.json.serial_candidate`; region/volume `game.json.disc.volume_identifier`;
status pill derived per `COMPATIBILITY_UX_MODEL.md` §2/§6 from
`game.json.native_build.playable`, `analysis.status`, `program.*`;
`Unknown` pill when `game.json` missing/unparseable.

**Error/recovery.** Missing `game.json` → card renders `Unknown` pill + "Evidence
missing"; clicking opens S12. Source ISO moved (`source_iso.path` gone) → pill
`Recovery required`, "Relink source ISO" (S12/S5).

---
## S2 — Library (empty)

**Purpose.** First-run onboarding; single, honest call to action.

**Layout.** Content region only: icon + one line ("No games in your library") +
`Add Game` primary button + one-line explanation that REPS2 needs one lawfully
owned ISO. Rail and top bar present as normal.

**States.** *Empty* only. If a job is running but the library is empty, show an
inline job link, not a fake card.

**Focus order.** skip-to-content → `Add Game` → rail.

**Actions.** `Add Game` → S3. Nothing else. No play, no scan prompt.

**Data fields.** None (no game artifact exists yet).

**Error/recovery.** Library root missing/not writable → `role="alert"` with the
resolved path and a "Choose library folder" action (S11).

---
## S3 — Add Game dialog

**Purpose.** Select exactly one owned raw ISO and run the import pipeline.

**Layout.** Modal (focus-trapped, scrim 40–60%, `MASTER.md` §7). Regions:
- Header "Add Game".
- File picker (native Windows picker; extension hint `.iso`).
- Pre-import validation line: detected layout, or a precise unsupported reason.
- Footer: `Cancel` (left, secondary) · `Import` (rightmost, primary, disabled
  until a file is chosen and validation is *not* a hard rejection).

**States.** *idle* · *validating* (spinner, `Import` disabled) · *duplicate*
(chosen ISO `source_iso.sha256` already in library → "Already imported as
`<display_name>`", `Import` disabled, `Open it` secondary) · *error*.

**Focus order.** file input → validation message → `Cancel` → `Import`. `Esc`
closes (cancel). Focus returns to the invoker on close (`S1` Add Game button).

**Actions & enablement.**
| Action | Enabled when |
|---|---|
| Import | a file is chosen AND layout is supported raw 2048-byte-sector ISO 9660 |
| Open existing | duplicate detected |
| Cancel | always |

**Data fields.** Chosen path (OS); `game.json.source_iso.path/.sha256` after
import; duplicate key = `source_iso.sha256`.

**Error/recovery.** Unsupported layout → precise, non-generic message: *"Unsupported
disc layout: raw 2352-byte BIN/CUE is not supported; provide a raw 2048-byte-sector
ISO 9660 image."* Same exact-reason treatment for CHD, UDF, and Joliet/Rock Ridge.
Import command failure → `REPS2_COMMAND_FAILED` (`reps2 --dev` diagnostics) →
inline error + "Open diagnostics" (S9) + Retry. Never partial-write: the pipeline
removes an incomplete output directory (`CLI.md`).

---
## S4 — Import & Build workflow

**Purpose.** Show transparent, coarse-grained pipeline stages for the selected
title; make progress honest and cancellable.

**Layout.** Content region replaces the dialog after Import. A **build pipeline
stepper** (`MASTER.md` §7) with one node per stage; each node: label, state,
timestamp, and a real duration when present. Right inspector: per-stage detail +
log tail (mono console view). Footer: `Cancel`, `Open job` (when a job exists).

**Stage nodes (frontend stages, exact IDs from `CLI.md`).** `elf.parse` →
`cfg.discover` → `ir.lift` → `codegen.emit_cpp` → (native build: CMake/MSVC
configure→compile→link) → build-identity write. Progress is **per stage**, not
per instruction. A determinate bar renders only if a stage emits a real fraction;
otherwise an indeterminate spinner on the running node.

**States.** Each node ∈ pending / running / succeeded / failed / skipped.
Screen-level: *running* · *succeeded* (→ S5) · *failed* (→ S12).

**Focus order.** stepper nodes (up/down) → `Cancel` → `Open job` → inspector log.
`Esc` = cancel-with-confirm if running.

**Actions & enablement.**
| Action | Enabled when |
|---|---|
| Cancel | a stage is running |
| Open job | a job record exists (S7) [PROTOTYPE-ONLY: no persistent queue today] |
| Open Game Detail | import succeeded |

**Data fields → source.** stage events `pipeline.stage.started/.succeeded/.failed`
with stable stage IDs (`reps2 --dev` diagnostics / summary.json); later stages and
counts from `<stem>.manifest.json` (`program.basic_block_count`,
`program.guest_instruction_count`, `program.unsupported_stop_count`,
`program.unresolved_indirect_target_count`, `translation.translation_unit_count`)
and `reps2-native-build-identity-<config>.json.cache_key_sha256`.

**Error/recovery.** `pipeline.stage.failed` named stage + `REPS2_PIPELINE_STAGE_FAILED`
records the scope, subsystem, duration, and original error. Analysis failures that
exceed a limit name the limit (`analysis.limits.max_blocks` etc.) and offer "Raise
limit / accept partial" (S12). Native build failure shows the MSVC/CMake stderr
from the inspector; identity file absent is an explicit failure, not a warning.
`Translation incomplete` (unsupported/unresolved counts > 0) does **not** block the
stage — counts are disclosed (`COMPATIBILITY_UX_MODEL.md` §3).

---
## S5 — Game Detail

**Purpose.** Per-title identity, evidence, limitations, and the next valid action.

**Layout.**
- Content header: placeholder cover, `display_name`, `serial_candidate` (mono),
  status pill + evidence one-liner + tooltip.
- Tabs: **Overview** (identity + analysis coverage + limitations),
  **Compatibility** (S6), **Evidence** (digests + last run result),
  **Limitations** (blockers/unsupported detail). Build/execution/diagnostics
  detail lives on S4, S8 and S10 rather than duplicated here.
- Action bar (right-aligned): next valid action(s) per lifecycle.
- Right inspector: evidence ledger summary (artifact list + digests).

**States.** *loading* · *populated* (one of the six lifecycle states) · each
failure/blocked variant (import-failed, analysis-failed, translation-incomplete,
native-build-failed, execution-blocked, unsupported, recovery-required) · *error*.

**Focus order.** tabs → action bar → inspector. `[`/`]` or left/right switch tabs.

**Actions (from `COMPATIBILITY_UX_MODEL.md` §4 matrix).**
| Action | Enabled from |
|---|---|
| View details | always |
| Analyze / re-analyze | always |
| Translate / re-translate | Analyzed+ |
| Native build | Translated+ |
| Run bounded execution probe | Native-built+ |
| **Play** | **Playable only.** In every other state the button is not rendered at all; Boot-tested offers "Run test session" and earlier states offer their next valid action, with a one-line note that Play requires Playable. |
| View execution report | Native-built+ |
| Relink source ISO | only when Recovery required |

**Data fields → source.** `game.json.display_name/.serial_candidate/.disc.*/.boot.*`;
`<stem>.manifest.json` counts + `translation.*` + `playable`;
`reps2-native-build-identity-<config>.json.cache_key_sha256`;
latest run report `outcome`/`session.*`. Digests rendered mono with copy.

**Error/recovery.** Any failure variant shows cause + named recovery, never a bare
"error". `recovery-required` disables downstream actions and offers relink to the
**same digest** (never silent rebind).

---
## S6 — Compatibility view (S5 tab)

**Purpose.** Lifecycle pill history + evidence ledger; prove *why* the state is
what it is.

**Layout.** Content: lifecycle pill (large) → required-evidence checklist (each
lifecycle state with met/unmet + artifact ref) → evidence ledger table. Right
inspector: selected ledger row detail.

**States.** One per lifecycle state; failure/blocked variants render the danger/
blocked tokens.

**Focus order.** checklist items → ledger rows → inspector.

**Actions.** `Export evidence bundle` [PROTOTYPE-ONLY: no bundler today] would
copy the referenced artifacts + digests; disabled with reason until implemented.

**Data fields → source.** State per `COMPATIBILITY_UX_MODEL.md` §2; met/unmet from
existence+parse of `game.json`, `<stem>.manifest.json`, identity JSON, run report.
Ledger rows name artifact, kind/schema, and digest.

**Error/recovery.** A checklist item whose artifact is absent renders "Unknown"
with the exact missing artifact name; never promoted. Points to S12.

---
## S7 — Build Queue

**Purpose.** All in-flight and recent jobs across titles.

**Layout.** Rail item `Build Queue`. Content: data table (sticky header, sortable
`aria-sort`, tabular numerics) — columns: title, stage, state, started, duration,
exit class [PROTOTYPE-ONLY: no persistent queue today]. Right inspector: job
detail (stage log + linked artifacts). `Open job` action.

**States.** *idle* · *running* · *done* · *failed* · [PROTOTYPE-ONLY] *empty*
("No jobs in this session").

**Focus order.** table header sort controls → rows → inspector → footer actions.

**Actions & enablement.** `Open job` (row selected) → S4/inspector. Cancel
(running rows). No Play anywhere here.

**Data fields → source.** Job state from `pipeline.stage.*` events + `summary.json`
(`outcome`, `diagnostic_count`, failure records with stable `code`/`failure_scope`/
`stage_id`). Durations from `summary.json`/events `duration`.

**Error/recovery.** Failed rows show stable diagnostic code (e.g.
`REPS2_PIPELINE_STAGE_FAILED`, `REPS2_COMMAND_FAILED`) + "Open report" → S10/S12.

---
## S8 — Execution / Session

**Purpose.** Run a bounded session and show the honest stop reason + live counters.

**Layout.** Reached from a game (not a rail item). Regions: status pill + action
bar (`Play`/`Run test session`/`Stop`); live counters panel; right inspector
(stop detail + linked report). Console view for streamed engine output.

**States.** *pre-run* · *running* · *budget_limit_reached* · *blocked* (guest stop
≠ completed) · *crashed* (host error) · *ended*.

**Focus order.** action bar → counters → console → inspector. `Esc` stops.

**Actions & enablement.**
| Action | Enabled when |
|---|---|
| Play | state `Playable` only; otherwise disabled + reason |
| Run test session | `Boot-tested` (replaces Play) |
| Stop | running / budget-exhausted batch loop |
| Open execution report | a run report exists (S10) |

**Data fields → source.** engine run report: `outcome`
(`completed`/`guest_stopped`/`budget_limit_reached`/`engine_error`),
`session.phase`, `session.pc`/`pc_hex`, `session.instructions_executed`,
`session.run_batches`, `session.elapsed_microseconds`, `session.media_present`,
`session.syscalls_seen`/`syscalls_handled`, `runs[].guest_status`/
`guest_status_code`/`pc_before`/`pc_after`/`instructions_this_run`,
`instruction_trace_pcs[]` (when `config.instruction_trace`). COP0 detail
(`cause`/`epc`/`cop0_status`/`badvaddr`) and `syscall_seq` come from the native
probe trace line, surfaced on S10.

**Error/recovery.** Guest stop statuses (`guest_status` ∈ `unresolved_control_flow`,
`unsupported_instruction`, `guest_exception`, `unhandled_syscall`,
`semaphore_wait_blocked`, …) are surfaced verbatim as the blocking reason — never
"completed", never Play. Host errors come from `rg.reps2.engine.error.v1` + exit
code class (2 invalid args, 3/4/5 input/module/session, 6 engine error, 10 host
exception). A zero exit code means a bounded run completed, **not** a boot.

---
## S9 — Developer Center

**Purpose.** Engine/analysis/build diagnostics index over `reps2 dev …`.

**Layout.** Rail item `Developer`. Content: sessions data table (id, outcome,
event/diagnostic counts, integrity warnings) + capabilities panel. Right inspector:
session detail. Actions: `Open report` (S10), `Compare` (two sessions).

**States.** *populated* · *empty* ("No diagnostic sessions under `<log_root>`").

**Focus order.** table → inspector → actions. `F5` refresh.

**Actions & enablement.** `Open report` (row selected) → S10; `Compare` (two rows
selected) → comparison view; `Show capabilities` → `reps2.developer.capabilities`.

**Data fields → source.** `reps2.developer.session_list` (`count`,
`available_count`, `truncated`, `sessions[]`), `session_inspection`
(`manifest`, `events[]`, `diagnostics[]`, `integrity_errors[]`),
`session_summary` (`outcome`, `event_count`, `diagnostic_count`, `failures[]`),
`session_comparison`, `capabilities`. `truncated:true` renders "older sessions
exist outside this view" (honest paging).

**Error/recovery.** `reps2.developer.error` envelope + non-zero exit → inline
error with the stable code; `REPS2_DEV_SUMMARY_UNAVAILABLE` explains a session
interrupted before finalization.

---
## S10 — Execution report (S9 detail)

**Purpose.** One session: stop status, PC, registers, syscalls, device state.

**Layout.** Content: header (title + outcome + digests) → summary grid → runs
table → trace/console. Right inspector: field detail with copy. `Export` in header.

**States.** *ok* (`outcome:completed`) · *blocked* (guest stop) · *error* (host).

**Focus order.** header actions → summary → runs rows → console → inspector.

**Actions.** `Export` (copy/emit the report JSON) [PROTOTYPE-ONLY: exporter not
implemented — disabled with reason] · `Back` to S9/S8.

**Data fields → source.** engine run report (`outcome`, `engine.*`, `module.*`,
`input.*`, `config.*`, `session.*`, `runs[]`, `instruction_trace_pcs[]`).
COP0 block (`cause`, `epc`, `cop0_status`, `badvaddr`, `exccode`, `syscall`) and
HLE block (`hle`, `syscalls`, `hle_handled`, `hle_number`, `hle_name`, `sema_*`,
`sif_*`, `syscall_seq`) come from the **native probe trace line** and render only
when that trace exists. Values are mono, tabular.

**Error/recovery.** Missing report artifact → "No run report for this session",
link to run a probe. Malformed report → parse error with exact file path.

---
## S11 — Settings

**Purpose.** Library paths, build storage, runtime defaults, input, logging, dev mode.

**Layout.** Rail item `Settings`. Content: grouped form (1100px max width),
left label / right control rows. Sections: Library, Build storage, Runtime
defaults, Input/controller, Logging/diagnostics, Developer mode. Footer: `Save`
(rightmost).

**States.** *defaults* · *modified* (`Save` enabled, "unsaved changes" chip) ·
*invalid* (`Save` disabled, field-level `role="alert"`).

**Focus order.** section by section, top-to-bottom; tab moves label→control;
`Save` last.

**Actions & enablement.**
| Action | Enabled when |
|---|---|
| Save | form valid AND modified |
| Revert | modified |
| Open log folder | log root resolvable |

**Data fields.** Library root, build storage path, runtime defaults
(`budget`/`max-batches`/`hle-syscalls` mirroring engine run report `config.*`),
input mapping, log root (`%LOCALAPPDATA%\REPS2\Logs` default; `REPS2_LOG_DIR`
override), developer mode. [PROTOTYPE-ONLY: no persisted app config file exists
today — settings are not yet durable.]

**Error/recovery.** Invalid path → inline error; Save blocked. Permission denied
on library root → actionable message + picker.

---
## S12 — Error & Recovery

**Purpose.** Failed stage + cause + recovery (inline or dialog).

**Layout.** Inline banner (`role="alert"`) at the failure's home screen, or a
dialog for blocking failures. Contents: cause line, named remediation, primary
recovery action, secondary "Open diagnostics" (S9), stable diagnostic code.

**States.** One variant per failure type: import failed, analysis failed,
translation incomplete (informational), native build failed, execution blocked,
unsupported, recovery required.

**Focus order.** cause → primary recovery → secondary. On dialog open, focus trap;
on close, restore focus to invoker.

**Actions & enablement.**
| Variant | Primary recovery |
|---|---|
| Import failed | Re-select ISO (unsupported-layout message shown) |
| Analysis failed | Raise named limit (`analysis.limits.*`) or accept partial |
| Native build failed | Retry; export generated project |
| Execution blocked / Unsupported | Open report; file runtime requirement |
| Recovery required | Relink source ISO (same digest only) |

**Data fields → source.** stable codes (`REPS2_COMMAND_FAILED`,
`REPS2_PIPELINE_STAGE_FAILED`), `summary.json.failures[]`,
run report `outcome`/`runs[].guest_status`, `game.json.source_iso.sha256`.

**Error/recovery.** Never a generic "something went wrong"; never silent success
for an unimplemented service (`COMPATIBILITY_UX_MODEL.md` §7).

---
## S13 — Command palette

**Purpose.** Keyboard/controller-driven action jump over the same focus model.

**Layout.** Centered overlay (max 640px), scrim, focus-trapped. Regions: query
input (focused on open) → ranked action/result list → footer hint
(`↑↓ move · Enter run · Esc close`).

**States.** *idle* (recent/suggested actions) · *querying* (filtered results,
`aria-live="polite"` count) · *empty* ("No matching actions").

**Focus order.** query input → result list (roving) → footer. `↑/↓`/d-pad move;
`Enter`/`A` runs; `Esc`/`B` closes and restores focus to invoker.

**Actions.** Any registry action subject to the **same** enablement rules as its
home screen (e.g. `Play` disabled unless Playable, with the same reason string).

**Data fields.** Action registry [PROTOTYPE-ONLY: registry not implemented today];
`Open game` entries from `game.json.display_name`.

**Error/recovery.** Running a now-disabled action re-checks enablement and shows
the reason; it never fires a stale action.

---
## Cross-screen rules

1. **Back navigation** restores previous screen incl. scroll, filter, selection
   (`INFORMATION_ARCHITECTURE.md` §3 `state-preservation`).
2. **Global loading/empty/error/offline** apply to every screen
   (`INFORMATION_ARCHITECTURE.md` §5). No screen requires network.
3. **Play invariant:** `Play` is rendered **only** in `Playable`. In every other
   state the button is absent: `Boot-tested` shows "Run test session" and earlier
   states show their next valid action, plus a one-line note that Play requires
   `Playable`. No title is Boot-tested or Playable today (baseline `cf34cf2`), so
   no screen renders an enabled Play (Downhill Domination USA = `Native-built`,
   latest probe `budget_exhausted` at `0x00246DAC` in a `PollSema` poll loop);
   the synthetic sample in `product-prototype/` is labelled and excluded from
   the aggregate "playable" count.
4. **Exit code ≠ success:** a screen never promotes state from a zero exit code;
   promotion requires the evidence in `COMPATIBILITY_UX_MODEL.md` §2.

---
## Backbone inconsistencies found

1. **Run report vs probe trace.** The brief lists `cause`/`epc`/`cop0_status`/
   `badvaddr` as `rg.reps2.engine.run_report.v1` fields; `engine/src/main.cpp` does
   **not** emit them. They belong to the generated **native probe trace line**
   (`rust/frontend/src/native_build.rs`); specs read COP0 there, stop/lifecycle
   from the run report.
2. **`game.json` ≠ `<stem>.manifest.json`.** `add-game` writes `game.json`
   (`schema_version:1`, `library.rs`); `recompile-elf` writes the build manifest
   (`manifest_version:3`, `build_manifest.rs`) with different field names
   (`analysis.status` vs `program.*`). Specs cite each correctly.
3. **Duplicate key.** This spec keys duplicates on `game.json.source_iso.sha256`
   and identity on `game.json.game_id`; the UX-model text does not name a key.
