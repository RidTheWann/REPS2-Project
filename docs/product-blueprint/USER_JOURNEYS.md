# REPS2 User Journeys

> Basis: `origin/main` `cf34cf2`. Screens are S1–S13
> ([`INFORMATION_ARCHITECTURE.md`](INFORMATION_ARCHITECTURE.md)); states are the six
> lifecycle states and the failure states of
> [`COMPATIBILITY_UX_MODEL.md`](COMPATIBILITY_UX_MODEL.md); requirements are `PR-###`
> ([`PRODUCT_REQUIREMENTS.md`](PRODUCT_REQUIREMENTS.md)). Personas: "Player" (A) and
> "Platform Engineer / QA" (B) ([`PRODUCT_DEFINITION.md`](PRODUCT_DEFINITION.md) §2).

Notation: **[A]** = class (`[E]`/`[P]`/`[PROP]`/`[N]`). Each journey states the
**entry screen**, the **steps** (UI action → system response → state shown), the
**success outcome**, and the **failure branch**. Journeys assume the library/service is
in its target shape; where a step depends on proposed work (`[PROP]`/`[N]`), the
prerequisite is named so the journey is not read as current capability.

---

## J1 — First run: add one owned ISO and reach an honest detail screen

**Persona:** A (Player). **Preconditions:** first launch; library empty; user owns one
PS2 ISO on local disk. **Entry:** S2 (Library empty). **Primary reqs:** PR-201…PR-206,
PR-101…PR-105. **Prereq:** S1 index + S4 streaming are `[PROP]` (PR-106, PR-203).

| # | UI action | System response | State shown |
|---|---|---|---|
| 1 | App opens on an empty library. | Empty state: icon + one line + primary action. | S2, no games. |
| 2 | Activate **Add Game**. | Dialog opens with a file picker and a supported-format note. | S3 idle. |
| 3 | Select the ISO. | Container validated (raw 2048-byte-sector ISO 9660); container type echoed. | S3 validating `[E]` (PR-202). |
| 4 | Confirm **Import**. | Pipeline begins: validate → extract boot ELF → analyze → translate → native build → register. | S4 stepper; each node pending/running/ok `[P]` stream (PR-203). |
| 5 | Watch (no action). | Non-fatal `Translation incomplete` surfaces unsupported/unresolved counts as info, not an error. | S4 info row `[E]` counts (PR-205). |
| 6 | Pipeline completes. | Record written at `<library>/games/<sha256>/`; ISO referenced by path, not copied. | S4 `ok`; auto-open S5. |
| 7 | Read the detail screen. | Identity, digests, evidence ledger, limitations, and the **next valid action**. | S5 with the true state (`Imported`…`Native-built`) `[PROP]` (PR-208). |

**Success:** the title appears in the library with a truthful status; task complete
without any manual `BOOT.ELF` step. Verify: `game.json` present, `playable:false`,
both digests bound (PR-103/PR-206); ISO bytes unchanged (PR-104).

**Failure branch — unsupported or unreadable input.** At step 3 the picker accepts a
BIN/CUE or CHD, or the ISO fails descriptor validation: show `Import failed` with the
exact reason ("2352-byte BIN/CUE, CHD, UDF, Joliet/Rock Ridge not supported",
`docs/CLI.md:155`) and a **Re-select** action (S12, PR-202). Incomplete output is
deleted; no library record is left (PR-204).

**Failure branch — analysis limit.** A stage fails naming the flag that raises it
(`--max-blocks`/`--max-instructions`/`--max-roots`, ADR 0030/0085). Offer retry with the
named override or explicit acceptance of partial analysis (PR-301).

---

## J2 — Inspect a built-but-blocked title

**Persona:** A + B. **Preconditions:** Downhill Domination USA (`SCUS_971.77`) is
`Native-built`; its latest executed probe stops `budget_exhausted` in a `PollSema`
(0x45) poll loop at `pc=0x00246DAC` after 4,999,998 instructions (ADR 0119 / PR #68,
`docs/PROJECT_STATE.md`). **Entry:** S1. **Reqs:** PR-109, PR-302, PR-402,
PR-502, PR-505.

| # | UI action | System response | State shown |
|---|---|---|---|
| 1 | Select the game card. | Card shows pill **Native-built** (cpu icon) + evidence line. | S1 `[PROP]` (PR-109). |
| 2 | Open the game. | Detail headline shows the highest evidenced state and the blocker. | S5. |
| 3 | Open the **Compatibility** tab. | Lifecycle pill history: `Imported → Analyzed → Translated → Native-built`, then an `Execution blocked` pill. | S6 `[PROP]` with tooltips (PR-702). |
| 4 | Read the evidence ledger. | Block/instruction/edge/unresolved counts from manifest v3; native build identity prefix; ABI `2`. | S6 `[E]` values (PR-302). |
| 5 | Open the **execution report**. | Stop status `budget_exhausted`, stop PC `0x00246DAC`, `PollSema` 0x45 poll loop on sema id 5, final PC + counts. | S10 blocked variant `[E]` (PR-402/PR-403). |
| 6 | Look for **Play**. | Play is **absent**; the next valid action is "Run test session". | S5/S8 gating `[PROP]` (PR-404). |

**Success:** the user understands *exactly* why the title cannot be played, and sees no
false Play affordance. No state above `Native-built` is claimed.

**Failure branch — report missing.** If the run report artifact is absent, S6 shows the
state as `Unknown` (never promoted) and S5 offers "Run a bounded session" (PR-401). The
UI performs no inference from the manifest alone (`COMPATIBILITY_UX_MODEL.md` §1).

---

## J3 — Run a bounded session and read the report

**Persona:** B (with A observing). **Preconditions:** title `Native-built`; module +
`ee-main-ram.bin` + source ISO present. **Entry:** S5 → S8. **Reqs:** PR-401…PR-408,
PR-501/PR-502. **Prereq:** in-app session control is `[P]`; the CLI runner exists `[E]`.

| # | UI action | System response | State shown |
|---|---|---|---|
| 1 | From detail, choose **Run test session**. | S8 opens pre-run with budget and batch controls (default 5,000,000 / 1). | S8 pre-run `[PROP]` (PR-405). |
| 2 | Start the run. | Engine session starts: bounded batches; live counters (instructions, PC). | S8 running; status bar shows an active job. |
| 3 | Observe the stop. | Run ends on a terminal guest status, not on "success". | S8 blocked/crashed/ended (PR-402). |
| 4 | Read the report. | Versioned report: final PC, instruction count, COP0 Cause/EPC/Status/BadVAddr, per-batch accounting, optional trace. | S10 `[E]` schema (PR-403). |
| 5 | Optionally **Stop** early. | Session closes cleanly; report written on stop. | S8 ended; deterministic shutdown `[PROP]` (PR-405). |

**Success:** a reproducible, machine-readable record of the bounded run with the exact
stop boundary. Verify: report `kind == rg.reps2.engine.run_report.v1`; status ∈ known
set; a zero host exit code is recorded as "bounded run produced a report", **not** as a
boot (`docs/CLI.md:75`).

**Failure branch — budget exhausted across batches.** `outcome == budget_limit_reached`
and guest still running: present "resume" (raise budget/batches) rather than a failure
(PR-401). **Failure branch — crash.** Engine crash ⇒ S8 `crashed` state + crash-report
path; S5 unchanged (no state promotion from a crash).

---

## J4 — Recover a moved ISO

**Persona:** A. **Preconditions:** an imported title whose source ISO has been moved or
altered. **Entry:** S1 with a `Recovery required` row. **Reqs:** PR-706, PR-108, PR-104,
PR-102. **Prereq:** relink is `[PROP]` (not implemented today).

| # | UI action | System response | State shown |
|---|---|---|---|
| 1 | Library loads. | One card shows a **Recovery required** pill (refresh icon) with a one-line cause. | S1 `[PROP]` (PR-706). |
| 2 | Open the game. | Detail shows the last evidenced stage, flagged stale, and a **Relink source ISO** action. | S5 + S12 inline `[PROP]`. |
| 3 | Choose the moved file. | REPS2 hashes the candidate by streaming and compares to the recorded digest. | S3-style validating (PR-206). |
| 4a | Digest matches. | Record's canonical path is updated; state returns to the previous stable stage. | Pill returns to e.g. `Native-built`. |
| 4b | Digest differs. | Relink refused; both digests shown; nothing rebinds. | S12 error, action stays disabled (PR-108). |

**Success:** the record is repaired to the *same* disc only, and the prior evidenced
stage is restored. Verify: canonical path updated, digest unchanged, no re-import.

**Failure branch — user selects a different disc.** Refuse silently-free: no state
change, both digests displayed, and the guidance is "this record is bound to a different
disc". Never bind a record to a different image (`docs/PRODUCT_VISION.md:71`).
**Failure branch — library root unwritable.** Surface the resolved path and the rule
(PR-102/PR-602).

---

## J5 — Review build history

**Persona:** B. **Preconditions:** several imports/builds performed across sessions.
**Entry:** S7 Build Queue (and S9 for persisted sessions). **Reqs:** PR-307, PR-305,
PR-309, PR-501. **Prereq:** persistent queue is `[PROP]`; there is no queue today.

| # | UI action | System response | State shown |
|---|---|---|---|
| 1 | Open **Build Queue**. | Table of in-flight and recent jobs across titles, with per-job stage + result. | S7 `[PROP]` (PR-307). |
| 2 | Select a job. | Inspector shows stage timeline, durations, and the named limit/flag on failure. | S7 inspector `[E]` stage data (PR-301). |
| 3 | Open a failed job. | Compiler/linker diagnostics shown; **Export generated project** offered. | S10 build-failed variant (PR-309). |
| 4 | Compare repeated builds. | Identity JSON (`cache_key_sha256`) for each config; identical inputs ⇒ identical key. | S10 identity view `[P]` (PR-305). |
| 5 | Inspect a prior session. | `dev sessions`/`dev summary` JSON for the exact invocation, echoed read-only. | S9 `[E]` (PR-502). |

**Success:** any build failure is explainable and reproducible from recorded artifacts;
cache identity is visible and deterministic.

**Failure branch — queue lost on restart.** The state is reported as lost explicitly
(no silent disappearance) and the last persisted session remains inspectable via S9
(PR-307/PR-501).

---

## J6 — Use developer diagnostics

**Persona:** B. **Preconditions:** `--dev`/`--log-dir` enabled (or dev mode set in S11).
**Entry:** S9 Developer Center. **Reqs:** PR-501…PR-505, PR-604, PR-603.

| # | UI action | System response | State shown |
|---|---|---|---|
| 1 | Open **Developer Center**. | Index of sessions with outcome and diagnostic counts. | S9 populated `[E]` (PR-501). |
| 2 | Open a session. | Event stream + diagnostics + integrity status; each failure carries stable `code`, `scope`, `stage_id`. | S10 detail `[E]` (PR-503). |
| 3 | Copy the **reproduction**. | `program` + exact `arguments` recorded at session start. | S10 reproduction block `[E]` (PR-502). |
| 4 | Compare two sessions. | `identical`/changed `command`/`outcome`; added/removed failure signatures with multiplicities. | S10 diff view `[E]` (PR-504). |
| 5 | Switch diagnostics root (S11). | New sessions (and inspections) use the selected root, matching `--log-dir`. | S11 + S9 reindex `[P]` (PR-604). |

**Success:** an engineer can re-run a known invocation and see exactly which failure
categories appeared or disappeared between revisions, without scraping log text
(`docs/CLI.md:147`).

**Failure branch — session predates `summary.json` or was interrupted.** Return the
`REPS2_DEV_SUMMARY_UNAVAILABLE` error envelope (or an explicit incomplete status with
integrity warnings), never a fabricated summary (PR-504, `docs/CLI.md:141`).
**Privacy note:** error text may contain local paths; the UI warns before "share"
(`docs/CLI.md:147`).

---

## J7 — Controller-only navigation

**Persona:** A. **Preconditions:** controller connected; nothing else required.
**Entry:** S1 (launch screen). **Reqs:** PR-705, PR-404, PR-109, PR-401.

| # | UI action | System response | State shown |
|---|---|---|---|
| 1 | Press any d-pad/face button. | Focus ring appears on the first card; shell never needs mouse/keyboard. | S1 focused `[PROP]` (PR-705). |
| 2 | D-pad to a card, press **A/Enter**. | Game detail opens; focus starts at the primary (next-valid) action. | S5. |
| 3 | Press **LB/RB**. | Top-level destinations cycle (Library / Add Game / Build Queue / Developer / Settings). | Rail switches. |
| 4 | Press **Y**. | Inspector toggles (selection/job/diagnostic detail). | S5/S7/S9 inspector. |
| 5 | Press **X**. | Command palette opens for action jump. | S13 `[PROP]`. |
| 6 | Navigate to **Run test session** (or **Play** if `Playable`). | The offered action matches the state; Play appears only when Playable. | S5/S8 `[PROP]` (PR-404). |
| 7 | Press **B/Esc**. | Back one level, restoring scroll, filter, and selection. | Previous screen restored (state preservation). |

**Success:** the entire S1→S5→S8 path — and every destination in the rail — is reachable
and operable with a controller alone, using the *same* focus order as keyboard
(`INFORMATION_ARCHITECTURE.md` §1).

**Failure branch — control unreachable.** Any control not focusable via D-pad is a
blocking accessibility defect (PR-705). **Failure branch — Play pressed while not
Playable.** Play is not rendered in that state, so the input is a no-op; the UI never
shows an error for an action it should not have offered (PR-404). **Failure branch —
reduced motion / 1024×720.** Layouts remain usable and motion collapses
(`prefers-reduced-motion`, `MASTER.md` §6, §9).

---

## Cross-journey invariants

1. **No state is promoted by the UI.** Every screen derives from artifacts only
   (PR-701); `Unknown` is first-class.
2. **Play exists only in `Playable`.** No journey may reach a running game today; J3/J7
   deliberately exercise the honest blocked/test path instead.
3. **Failures name a cause and a recovery.** J1 (import), J4 (recovery), J5 (build),
   J6 (diagnostics), J3 (execution) each end in a specific, actionable message.
4. **Nothing is faked to "finish" a journey.** A blocked title stays blocked; the
   product's value in the blocked case is precise, reproducible disclosure.
