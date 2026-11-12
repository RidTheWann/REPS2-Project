# REPS2 Product Requirements

> Basis: `origin/main` `cf34cf2`. Companion documents:
> [`PRODUCT_DEFINITION.md`](PRODUCT_DEFINITION.md),
> [`COMPATIBILITY_UX_MODEL.md`](COMPATIBILITY_UX_MODEL.md) (canonical states),
> [`INFORMATION_ARCHITECTURE.md`](INFORMATION_ARCHITECTURE.md) (S1–S13),
> [`USER_JOURNEYS.md`](USER_JOURNEYS.md) (J1–J7).
> Source of truth for capability/CLI: `docs/CAPABILITIES.md`, `docs/CLI.md`,
> `docs/ROADMAP.md`, `docs/COMPATIBILITY_MATRIX.md`, `docs/decisions/`.

## Conventions

- **Class:** `[E]` existing & evidenced · `[P]` partial · `[PROP]` proposed ·
  `[N]` not implemented · `[UNKNOWN]` no evidence.
- **Layer:** `UI` · `App service` (application service owning the subprocess/JSON
  boundary) · `CLI` (frontend CLI) · `Engine` (RG REPS2 ENGINE) · `Runtime` (C++20
  shared runtime). No screen may call Rust recompiler or engine internals directly
  (`INFORMATION_ARCHITECTURE.md` §4).
- **State reference:** lifecycle `Imported → Analyzed → Translated → Native-built →
  Boot-tested → Playable`; failure states `Import failed`, `Analysis failed`,
  `Translation incomplete`, `Native build failed`, `Execution blocked`, `Unsupported`,
  `Recovery required` (`COMPATIBILITY_UX_MODEL.md` §2–3).
- **Evidence categories:** Observed / Derived / Inferred / Unknown (`AGENTS.md`).
- A requirement is satisfied only when its **verification method** passes with real
  output. A plausible implementation or a synthetic test does not satisfy a real-title
  gate (`AGENTS.md`).

---

## A. Library

Owns the persistent catalog of imported titles and the canonical state each card shows.
The directory layout, manifest, and dedup rule already exist; the *service* does not.

| ID | Requirement | Rationale | Layer | Class | Verification | Failure UX |
|---|---|---|---|---|---|---|
| PR-101 | The library is content-addressed at `<library>/games/sha256-<iso-digest>/` holding `game.json`, and `boot.elf`/`recompiled.cpp`/`ee-main-ram.bin` only when the corresponding stage succeeded. | Stable identity independent of title serial; per-stage artifacts absent, not faked. | App service | `[E]` | `rust/frontend/src/library.rs` unit tests; inspect an imported dir. | Missing artifact ⇒ stage not shown; never an empty file. |
| PR-102 | Default root `%LOCALAPPDATA%\REPS2\Library`; override via `REPS2_LIBRARY_DIR` or `--library <dir>`. | Per-user, offline, no elevation; explicit override for QA. | CLI + App service | `[E]` | `reps2 add-game <iso> [--library]`; env override smoke. | Unwritable root ⇒ `Import failed` with the resolved path. |
| PR-103 | `game.json` is schema v1 and always carries `playable:false` + `playability_status:"blocked_runtime_incomplete"`. | The record must not over-claim; runtime incompleteness is the default truth today. | App service | `[E]` | Parse an imported `game.json`; assert both fields. | Absent/unparseable ⇒ state `Unknown`, no promotion. |
| PR-104 | The source ISO is referenced by **canonical path**, never copied. | Multi-GB images stay user-owned; no duplicate storage. | App service | `[E]` | Import then confirm ISO bytes unchanged and no copy exists. | Path vanished ⇒ `Recovery required` (see PR-706). |
| PR-105 | Duplicate import is detected by content hash and does not create a second record. | Deterministic catalog; safe re-runs. | App service | `[E]` | Import the same ISO twice; assert single record. | Duplicate ⇒ S3 shows the existing entry, not a second one. |
| PR-106 | The library exposes a **list/index** query (state, title, serial, digests, timestamps) without re-parsing each game's internals. | S1 must render a grid; today no list API exists. | App service | `[PROP]` | Index build over N imported games; assert counts match directories. | Unreadable record ⇒ row shows `Unknown` + repair action. |
| PR-107 | The library supports **remove** (record only; never the user's ISO) and **rescan** (reconcile disk vs index). | Recovery and hygiene; records may drift. | App service | `[PROP]` | Remove/rescan round-trip test. | Remove ⇒ confirm dialog, record-only warning. |
| PR-108 | The library supports **relink** to a moved ISO **only** when the content hash matches (PR-706). | Never rebind to a different disc silently (`docs/PRODUCT_VISION.md:71`). | App service | `[PROP]` | Relink same-digest vs wrong-digest fixture. | Digest mismatch ⇒ refuse with both digests shown. |
| PR-109 | Every card shows the canonical status pill derived from the state tuple, never a raw stage count. | Truthfulness; single renderer (`MASTER.md` §7–8). | UI | `[PROP]` | Snapshot S1 against a fixture set covering all 6 states + failures. | Unknown artifact ⇒ neutral pill + tooltip. |

## B. Add Game / Import

The one-ISO entry point. The command exists; the orchestration/streamed progress does not.

| ID | Requirement | Rationale | Layer | Class | Verification | Failure UX |
|---|---|---|---|---|---|---|
| PR-201 | Add Game accepts exactly one owned ISO and runs the whole import internally; the user never extracts `BOOT.ELF`. | The core promise (`docs/PRODUCT_VISION.md:16`). | CLI → App service | `[E]` command / `[PROP]` orchestration | `reps2 add-game <iso>` end-to-end; assert record + no user file steps. | Non-ISO/unsupported container ⇒ S3/S12 with exact reason. |
| PR-202 | Import selects the ISO via a native file picker and validates the container (raw 2048-byte-sector ISO 9660) before any write. | Fail before side effects; unsupported formats must be rejected explicitly. | UI (`S3`) → CLI | `[P]` | Add with BIN/CUE/CHD/UDF fixtures ⇒ all refused. | `Import failed`: "2352-byte BIN/CUE, CHD, UDF, Joliet/Rock Ridge not supported" (`docs/CLI.md:155`). |
| PR-203 | Import streams **per-stage** status (validate → extract → analyze → translate → build → register), each `pending/running/ok/failed/skipped`, with no fabricated percentage. | Transparent pipeline; S4 stepper (`MASTER.md` §7). | App service | `[E]` events / `[P]` stream API | `--dev` `pipeline.stage.*` events; assert stable stage IDs (`docs/CLI.md:110`). | Stage failure ⇒ S4 marks the node failed, points to S12. |
| PR-204 | Import cleans incomplete output on failure and never overwrites an existing output directory. | No half states; source untouched. | CLI | `[E]` | Failure-injection test; assert output removed, ISO unchanged. | Retry offered; no partial record left in the library. |
| PR-205 | Import proceeds after non-fatal `Translation incomplete`, disclosing unsupported-instruction and unresolved-target counts. | Real titles always carry some gaps; blocking would be wrong (`COMPATIBILITY_UX_MODEL.md` §5.3). | App service | `[E]` counts / `[PROP]` surface | Read counts from manifest v3 (`docs/CLI.md:187`). | Disclosed as an info row, not a failure. |
| PR-206 | Import binds the exact ISO SHA-256 to the extracted boot-ELF SHA-256. | Reproducibility; disc identity (`docs/CLI.md:54`). | CLI | `[E]` | Compare `parent_iso_sha256` + ELF digest to recomputed values. | Mismatch ⇒ refuse, do not register. |
| PR-207 | Import never requires network or a proprietary BIOS. | Offline library; clean-room HLE goal (`docs/PRODUCT_VISION.md:69`). | App service | `[E]` offline / `[PROP]` no-BIOS | Offline run; assert no outbound connection. | Network absent ⇒ no change in behavior. |
| PR-208 | On success the user lands on the game's detail screen with the honest current state. | Import ≠ playable; the UI must say so immediately. | UI (`S4→S5`) | `[PROP]` | Post-import navigation + state assertion. | Imported-only record ⇒ detail shows `Imported` and next valid action. |

## C. Build pipeline

Analysis → translation → native build → module. All commands exist; orchestration and
cache reuse are proposed.

| ID | Requirement | Rationale | Layer | Class | Verification | Failure UX |
|---|---|---|---|---|---|---|
| PR-301 | Analysis is bounded by explicit, validated limits with overrides that a limit failure names. | No unbounded work; actionable errors (`docs/ROADMAP.md:102`, ADR 0030/0085). | CLI / Engine | `[E]` | Limit-exhaustion test; assert the flag is named. | `Analysis failed` with the raising flag + current value. |
| PR-302 | Analysis reports blocks, instructions, static edges, unresolved indirect targets, unsupported stops, and exceptions for the exact boot-ELF digest. | The state tuple's `Analyzed` evidence. | CLI | `[E]` | Parse manifest v3; assert counts (`docs/CLI.md:187`). | Absent manifest ⇒ `Analyzed` not promoted. |
| PR-303 | Translation emits deterministic C++20 translation units plus a timestamp-free manifest that always records `playable:false`. | Byte-identical re-runs; honest claim (`docs/CLI.md:187`). | CLI | `[E]` | Two identical invocations ⇒ byte-identical manifest. | Non-determinism ⇒ release-blocking defect. |
| PR-304 | `recompile-iso` runs the whole preparation from one ISO with implicit `--parent-iso` and refuses an existing output dir. | One-command path; provenance binding. | CLI | `[E]` | `recompile-iso` into a fresh dir; re-run refused. | Existing dir ⇒ refuse; failed prep cleans up (PR-204). |
| PR-305 | Native build records a configuration-specific identity (`cache_key_sha256`) covering boot-ELF/generated-source/RAM/toolchain/runtime-ABI/schemas. | Future cache reuse; staleness rejection (ADR 0083/0102, `docs/CLI.md:217`). | Engine / CLI | `[P]` | Inspect `reps2-native-build-identity-<config>.json`; recompute key. | Identity absent ⇒ `Native build failed`, no `Native-built`. |
| PR-306 | The generated module declares engine ABI and runtime ABI; a mismatch fails closed before a session starts. | Never run an incompatible module (`docs/CLI.md:69`). | Engine | `[E]` | Load a module with a wrong ABI ⇒ refused with reason. | ABI mismatch ⇒ S10 error variant; no execution. |
| PR-307 | A persistent **build queue** tracks in-flight/recent jobs across titles with resumable status. | S7 exists in the IA but no queue exists (`INFORMATION_ARCHITECTURE.md` §4). | App service | `[PROP]` | Queue survives app restart; job state round-trips. | Job loss on restart ⇒ explicit, never silent. |
| PR-308 | The build pipeline reuses a cached module **only** when the full cache identity matches. | A same-serial revision must not reuse stale code (`docs/PRODUCT_VISION.md:75`). | App service / Engine | `[PROP]` | Cache-hit vs cache-miss fixture; tamper a key. | Stale ⇒ rebuild; log the reason. |
| PR-309 | Build failures surface compiler/linker diagnostics and allow exporting the generated project. | Repair + handoff; users may not have MSVC (`COMPATIBILITY_UX_MODEL.md` §3). | UI (`S10`) | `[P]` | Trigger a compile error; assert stderr shown + export path. | Missing toolchain ⇒ `Native build failed` + toolchain guidance. |

## D. Execution

Bounded probe and (eventual) Play. The runner exists; session management does not.

| ID | Requirement | Rationale | Layer | Class | Verification | Failure UX |
|---|---|---|---|---|---|---|
| PR-401 | Execution is always bounded by an instruction budget and a batch cap; the runner emits one versioned run report. | No unbounded guest execution (`docs/CLI.md:72`–75). | Engine | `[E]` | Run with `--budget`/`--max-batches`; parse `rg.reps2.engine.run_report.v1`. | Budget hit ⇒ `budget_limit_reached`, resumable. |
| PR-402 | Guest stop statuses (`unhandled_syscall`, `semaphore_wait_blocked`, `guest_exception`, `unsupported_instruction`, `unresolved_control_flow`) are surfaced verbatim, never as "completed". | The exact reason a title can't progress (`docs/CLI.md:75`). | UI (`S8`) / Engine | `[E]` | Assert status + stop PC from a real probe (e.g. DD). | Non-success ⇒ `Execution blocked`; Play absent (PR-404). |
| PR-403 | The run report records final PC, instruction count, budget, COP0 Cause/EPC/Status/BadVAddr, and (opt-in) a bounded trace. | Diagnostic completeness (`docs/CLI.md:209`). | Engine | `[E]` | Field-presence assertions on the JSON report. | Missing fields ⇒ schema violation, reported. |
| PR-404 | **Play** is enabled **only** in `Playable`; a `Boot-tested` title offers "Run test session"; earlier states offer the next valid action. | The central honesty rule (`COMPATIBILITY_UX_MODEL.md` §4, §6). | UI (`S1`,`S5`,`S8`) | `[PROP]` | Render each of the 6 states from fixtures; assert Play gating. | Attempted Play when not `Playable` ⇒ action absent, not an error. |
| PR-405 | A bounded session run is startable/observable from S8 with live counters and an explicit Stop. | Session control; today only a CLI runner exists. | App service → Engine | `[P]` runner / `[PROP]` session control | Start/stop a probe; assert report written on stop. | Engine crash ⇒ `crashed` state + crash report path. |
| PR-406 | The raw-ISO media boundary is host-owned; the runtime never opens files itself. | Security + portability (`docs/CLI.md:71`). | Runtime / Engine | `[E]` | `--media` sector-read test with range checks. | Media absent ⇒ media ops fail closed. |
| PR-407 | Unsupported runtime services produce a structured requirement (service number + PC), never a fabricated `$v0`. | Evidence-driven backlog (`COMPATIBILITY_UX_MODEL.md` §3 "Unsupported"). | Runtime | `[E]` | Probe reports `unhandled_syscall` + number. | Must not be masked as success. |
| PR-408 | `--hle-syscalls` enables **only** implemented services; the label never implies kernel/IOP completeness. | Prevents an implied-capability lie (`docs/CLI.md:73`). | Engine | `[E]` | Toggle on/off; compare stop boundary. | Unknown service ⇒ `unhandled_syscall`. |

## E. Developer Center

| ID | Requirement | Rationale | Layer | Class | Verification | Failure UX |
|---|---|---|---|---|---|---|
| PR-501 | `--dev`/`--log-dir` produce a session directory with `session.log`, `events.jsonl`, `session.json`, `summary.json`. | Reproducible triage (`docs/CLI.md:105`–110). | CLI | `[E]` | Run any command with `--dev`; assert the four files. | Interrupted session ⇒ incomplete + integrity warning. |
| PR-502 | `dev sessions|inspect|summary|compare|capabilities` each emit exactly one versioned JSON report, no human scraping. | Agent/IDE consumption (`docs/CLI.md:127`–137). | CLI | `[E]` | `dev --help` grammar + one JSON doc per command. | Malformed ⇒ JSON error envelope, non-zero exit. |
| PR-503 | Diagnostic codes are matched by stable ID, not message text (`REPS2_PIPELINE_STAGE_FAILED`, `REPS2_COMMAND_FAILED`, …). | Stable automation (`docs/CLI.md:139`). | CLI | `[E]` | `dev capabilities` catalog; assert codes present. | Unknown code ⇒ still rendered verbatim. |
| PR-504 | `dev compare` reports identical/changed command/outcome and added/removed failure signatures with multiplicities. | Regression triage between revisions (`docs/CLI.md:143`). | CLI | `[E]` | Compare two sessions; assert signature sets. | Summary unavailable ⇒ `REPS2_DEV_SUMMARY_UNAVAILABLE`. |
| PR-505 | The Developer Center surfaces run reports and diagnostics without claiming a boot stage. | Keeps the engine a tool surface, not an oracle. | UI (`S9`,`S10`) | `[P]` | Render a DD report (blocked) with no "playable" affordance. | Report absent ⇒ empty state + how to produce one. |

## F. Settings

| ID | Requirement | Rationale | Layer | Class | Verification | Failure UX |
|---|---|---|---|---|---|---|
| PR-601 | A persistent settings store exists for library root, build storage, runtime defaults, input, logging, dev mode. | S11 is specified; no store exists (`INFORMATION_ARCHITECTURE.md` §4 `N`). | App service | `[N]` | Settings round-trip across restarts. | Corrupt store ⇒ defaults + warning, never a crash. |
| PR-602 | Library root is configurable and equivalent to `--library`/`REPS2_LIBRARY_DIR`. | One source of truth for the catalog location. | App service | `[PROP]` | Change root; assert S1 reindexes. | Unwritable root ⇒ invalid-state + reason. |
| PR-603 | Build storage location is configurable (default per-user cache). | Large generated artifacts; disk planning. | App service | `[PROP]` | Point at an alternate dir; assert artifacts land there. | Insufficient space ⇒ pre-flight warning. |
| PR-604 | Dev mode and log root are configurable and map to `--dev`/`--log-dir`. | Parity with CLI diagnostics. | App service / CLI | `[P]` | Toggle dev mode; assert a session directory appears. | Root unwritable ⇒ logging disabled with notice. |
| PR-605 | No setting may enable a fabricated capability or bypass an evidence gate. | Settings are not a lie switch. | App service | `[PROP]` | Review: no option flips a state. | Any such option ⇒ design defect. |
| PR-606 | Every setting change is validated and reversible; invalid values are refused with the rule shown. | Predictable behavior; no silent coercion. | App service | `[PROP]` | Invalid-input tests per field. | Refusal shown in-place near the field. |

## G. Platform / State

| ID | Requirement | Rationale | Layer | Class | Verification | Failure UX |
|---|---|---|---|---|---|---|
| PR-701 | One state-derivation function maps artifacts → the state tuple for all screens. | Single truth source; no per-screen logic (`COMPATIBILITY_UX_MODEL.md` §1). | App service | `[PROP]` | Table-driven test over artifact fixtures. | Missing artifact ⇒ `Unknown`, never promoted. |
| PR-702 | Forward-only promotion; downgrade is explicit when artifacts are invalidated. | Prevents a stale high state (`COMPATIBILITY_UX_MODEL.md` §5.2). | App service | `[PROP]` | Invalidate an artifact; assert regression to the highest evidenced stage. | Regression ⇒ visible reason, no auto-rebuild lie. |
| PR-703 | Actions strictly downstream of a failed stage are suppressed, but visible with a reason. | WCAG `empty-nav-state`; no dead buttons. | UI | `[PROP]` | Failure fixture per stage; assert gating + reason. | Missing reason ⇒ accessibility defect. |
| PR-704 | Status is conveyed by icon + label + token, never colour alone. | `color-not-only` (`MASTER.md` §2.6, §9). | UI | `[PROP]` | Contrast + non-colour assertions per pill. | Colour-only ⇒ a11y failure. |
| PR-705 | The full shell is keyboard- and controller-operable with one focus model. | `INFORMATION_ARCHITECTURE.md` §1; `MASTER.md` §9. | UI | `[PROP]` | Controller-only pass over S1→S5→S8; 1024×720 keyboard pass. | Unreachable control ⇒ blocking defect. |
| PR-706 | `Recovery required` when a referenced ISO is missing/changed; relink only to the same digest. | Never rebind silently (`COMPATIBILITY_UX_MODEL.md` §3). | App service | `[PROP]` | Move/alter the ISO; assert the state and relink gate. | Wrong digest ⇒ refuse with both digests. |
| PR-707 | Multi-title is a platform property: two distinct titles traverse the same frontend/backend/ABI. | M6 gate (`docs/ROADMAP.md:39`). | Engine / Runtime | `[PROP]` | Two title records, same toolchain, independent traces. | Per-title fork of core semantics ⇒ reject in review. |
| PR-708 | No screen requires network; the catalog is fully offline. | Offline-first (`INFORMATION_ARCHITECTURE.md` §5). | UI / App service | `[E]` offline / `[PROP]` UI | Network-isolated run of S1–S11. | Network required ⇒ defect. |

---

## H. Governance questions applied to one representative feature

Seven governance questions, applied to **PR-201 / PR-202 — "Add Game: import one owned
ISO"**, the feature that most directly carries the core promise.

1. **What is the source of truth for this feature's behaviour?**
   `rust/frontend/src/library.rs` (record layout, dedup) + ADR 0010 +
   `docs/CLI.md` §"add-game" (accepted container, canonical path, `playable:false`).
   The UI must not add rules the CLI does not implement (Rule: UI reads only artifacts).
2. **What evidence class underpins each claim?**
   *Observed:* an import writes `game.json` (schema v1) with the source ISO's SHA-256.
   *Derived:* a title is `Imported` because that manifest exists for that digest.
   *Inferred:* S3's "validating → importing" transition in the UI. `Unknown:` anything
   about the title's runtime behavior at import time — intentionally unknown.
3. **What is the capability class, stated honestly?**
   `add-game` is `[E]`; the persisted library service, duplicate UX on S3, and the
   `S4` streamed stepper are `[PROP]`. S3 must not present proposed orchestration as
   existing (`PR-203`).
4. **Which layer owns it?**
   `CLI` owns parsing/import today; the `App service` owns the subprocess/JSON boundary
   and the streamed events; `UI` owns S3/S4 rendering only. No screen parses CLI text
   (`INFORMATION_ARCHITECTURE.md` §4).
5. **How is it verified, and what would falsify it?**
   Verify: import a fixture ISO → assert `<library>/games/sha256-<digest>/game.json`, both
   digests, `playable:false`, and that the ISO bytes are unchanged (PR-104, PR-106).
   Falsify: a second record for the same digest (PR-105), a copied ISO (PR-104), or a
   record without the digest binding (PR-206).
6. **What is the failure and recovery UX?**
   Unsupported container or unreadable file ⇒ `Import failed` with the specific reason
   and re-select (S12). Incomplete output is cleaned (PR-204). A moved ISO after import
   ⇒ `Recovery required` with relink gated on digest equality (PR-706/PR-108).
7. **What is the explicit non-goal / scope boundary here?**
   Import does **not** claim the title is playable or even booted; the record is written
   `playable:false` (`PR-103`). Import does not transcode/copy the ISO, require network or
   a BIOS (PR-207), or implement per-title workarounds. Any of these would violate
   `PRODUCT_DEFINITION.md` §5.

---

## I. Requirement → screen → journey map

| Area | Screens | Journeys |
|---|---|---|
| A Library | S1, S2, S5, S6 | J1, J2, J4, J5 |
| B Add Game / Import | S2, S3, S4 | J1, J4 |
| C Build pipeline | S4, S7, S10 | J1, J5, J6 |
| D Execution | S5, S8, S10 | J3, J7 |
| E Developer Center | S9, S10 | J2, J3, J6 |
| F Settings | S11 | J4, J6 |
| G Platform / State | S1, S12, S13 (all) | J1–J7 |
