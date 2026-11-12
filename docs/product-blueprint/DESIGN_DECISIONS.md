# REPS2 Product & Design Decisions

Architecture Decision Records for the Session 2 product blueprint. These are
**product/design** decisions, deliberately separate from the repository's engine
ADRs in `docs/decisions/`. Format: Context · Decision · Alternatives ·
Consequences · Status.

Repository baseline for this record: `origin/main` `cf34cf2`, branch
`feat/product-blueprint`.

---

## DD-001 — Re-author the generator palette into "Studio Dark"

**Context.** The UI/UX Pro Max generator (`search.py --design-system --persist
-p REPS2`) produced an "Accessible & Ethical" system whose component CSS was
light-oriented and internally contradictory for a dark desktop product: a
`#1E293B` secondary button on a `#0F172A` background is invisible, modals were
`background:#fff`, and inputs used a light border on a dark surface. Its
accessibility foundations (focus rings, contrast rules, reduced motion, 4/8px
spacing, skip links) are correct and worth keeping.

**Decision.** Keep the generator's accessibility baseline and Inter typography,
but re-author the palette and component specs into a single dark "Studio Dark"
language (`design-system/reps2/MASTER.md`), grounded in two generator research
results: the *Modern Dark (Cinema)* style and the *Developer Tool / IDE* dark
palette. Reject the neon/cyberpunk/HUD results, which fail contrast and match the
directive's forbidden list.

**Alternatives.** (a) Accept the generator output verbatim — rejected, unusable
for dark desktop. (b) Adopt a cyberpunk/HUD aesthetic — rejected, poor
accessibility and explicitly forbidden. (c) Design a novel palette with no
research anchoring — rejected, slower and less defensible.

**Consequences.** One coherent dark token system (`tokens.css`, `tokens.json`)
that a WinUI/WPF implementation can consume directly. Light theme deferred.

**Status.** Accepted.

---

## DD-002 — The lifecycle is derived from versioned artifacts, never invented

**Context.** The directive requires a truthful state model, and the repo already
publishes `Imported → Analyzed → Translated → Native-built → Boot-tested →
Playable`, with "a prior stage never implies a later one".

**Decision.** The product layer derives state only from these artifacts:
`game.json` (schema v1), `<stem>.manifest.json` (schema v3),
`reps2-native-build-identity-<config>.json`, the engine run report
(`rg.reps2.engine.run_report.v1`), and `reps2 dev` reports (developer-report v1).
"Unknown" is a first-class value: a stage is not promoted when its evidence is
absent. Failure states (`Import failed`, `Analysis failed`,
`Translation incomplete`, `Native build failed`, `Execution blocked`,
`Unsupported`, `Recovery required`) each map to a concrete backend signal.

**Alternatives.** (a) Infer state from process exit code — rejected; the repo
explicitly forbids exit-code-as-success. (b) Let the UI cache its own state —
rejected; it would drift from evidence.

**Consequences.** The UI cannot show a state the backend cannot reproduce. Play
is disabled everywhere except `Playable`. Documented in
`COMPATIBILITY_UX_MODEL.md`.

**Status.** Accepted.

---

## DD-003 — Production GUI technology: WinUI 3, with WPF fallback

**Context.** The repo mandates that the desktop shell call the same
library/build/session API as the CLI, and forbids per-instruction FFI. Every
candidate therefore integrates by **subprocess + versioned JSON**
(`reps2 …`, `rg-reps2-engine …`, `reps2 dev …`).

**Decision.** Primary: **WinUI 3 (Windows App SDK)**. Fallback: **WPF (.NET)**.
Alternative if `.NET` must be avoided entirely: **Tauri** (reuses the CSS design
tokens, at the cost of a WebView2 runtime + JS/TS lane and weaker native a11y).

**Rationale.** Windows-first matches the only runtime-validated target; GPU
composition supports a 60fps game grid; UIA + `Windows.Gaming.Input` satisfy the
controller-first, accessible requirement; no engine change and no FFI are needed.
Cost: one new `.NET` toolchain lane in CI plus Windows App SDK packaging.

**Rejected.** egui/iced (weak UIA/IME — poor fit for a screen-reader-required
controller UI); Qt/C++ (large dependency + licensing friction in a GPLv3 repo).

**Status.** Accepted (subject to a dependency spike when the M7 library API
freezes).

---

## DD-004 — The prototype is dependency-free static HTML/CSS/JS

**Context.** The directive requires a functional prototype quickly, labelled as
a design artifact, without connecting mock controls to unfinished runtime APIs.

**Decision.** `product-prototype/` is a single-page app with no build step and
no dependencies, consuming `design-system/reps2/tokens.css`. It runs by opening
`index.html` or serving the folder. Every simulated action is visibly labelled.

**Alternatives.** (a) Prototype directly in WinUI 3 — rejected; too slow to
iterate and premature before the library API freezes. (b) A React framework
prototype — rejected; adds a toolchain for a throwaway artifact.

**Consequences.** The prototype validates the design system and interaction
model; it is not the production shell and must not be shipped as one.

**Status.** Accepted.

---

## DD-005 — Inter for UI, JetBrains Mono for technical data

**Context.** REPS2 is both a game library and an engineering console. Hex
addresses, digests, register values, PCs and syscall numbers must align.

**Decision.** `Inter` for all prose/headings; `JetBrains Mono` (with
`font-variant-numeric: tabular-nums`) for addresses, digests, hex, register
values, paths, console output and syscall numbers. Body text never uses mono.

**Alternatives.** All-mono "terminal" system — rejected; hurts long-form
readability for a consumer-facing library. Two variable fonts — deferred.

**Status.** Accepted.

---

## DD-006 — One canonical status pill; colour is never the only signal

**Context.** Nine lifecycle/failure states must be distinguishable quickly
without confusing the user about severity vs. stage.

**Decision.** A single `pill` component renders every state as icon + label +
status token, with a tooltip stating the exact evidence requirement. Colour is
always paired with an icon and text label (WCAG `color-not-only`).

**Status.** Accepted.

---

## DD-007 — Actions are gated strictly by stage evidence

**Context.** It is easy to offer "Play" optimistically; doing so would
misrepresent an unplayable title.

**Decision.** Action availability follows the matrix in
`COMPATIBILITY_UX_MODEL.md`: `Play` only in `Playable`; `Run bounded session`
only with a `Native-built` module; downstream actions are suppressed when an
upstream stage failed. Disabled actions remain visible with a stated reason.

**Status.** Accepted.

---

## DD-008 — The GUI owns no recompiler logic

**Context.** `docs/ARCHITECTURE.md` and `PRODUCT_VISION.md` fix the boundary.

**Decision.** The GUI talks only to application services that own the
subprocess/JSON boundary; no GUI code parses ELF, decodes instructions, lifts
IR, generates C++, or links modules. The same services back the CLI, so
automation tests the exact path the UI uses.

**Status.** Accepted.

---

## DD-009 — Offline-first library; enrichment is optional

**Context.** `PRODUCT_VISION.md` states the library must work offline and must
not depend on scraping services.

**Decision.** No screen requires network access. Cover art is generated
placeholders in the prototype; any future metadata enrichment is strictly
optional and non-blocking.

**Status.** Accepted.

---

## DD-010 — Blueprint isolation & repository hygiene

**Context.** Session 1 is actively editing engine/runtime/compiler code and some
shared docs.

**Decision.** Session 2 deliverables live only under `docs/product-blueprint/`,
`design-system/reps2/`, and `product-prototype/`. No engine, runtime, compiler,
generated-code contract, ADR, `README.md`, `ROADMAP.md`, or `PROJECT_STATE.md`
file was modified. No ISO, boot ELF, BIOS, or copyrighted asset is included;
covers are generated and sample titles are labelled synthetic.

**Status.** Accepted.

---

## DD-011 — Record, do not silently "fix", observed repository inconsistencies

**Context.** Independent research during this session surfaced documentation
disagreements inside the repository (Session 1's domain).

**Decision.** Product docs record these as **Observed** and do not edit the
affected files. Noted items:

1. **Frontier drift.** `docs/COMPATIBILITY_MATRIX.md` still records Downhill
   stopping `semaphore_wait_blocked` on `WaitSema (0x44)` at `0x0025A724`
   (3,169,504 instructions, ADR 0101), while `docs/PROJECT_STATE.md` records the
   later executed probes through ADR 0119 / PR #68: `budget_exhausted` at
   `pc=0x00246DAC` after 4,999,998 instructions, spinning in a `PollSema` (0x45)
   loop on semaphore id 5 (`poll_sema=26479`, `sema_op_id=5`, `syscalls=26591`,
   `hle_handled=26574`; `sif_rpc_bind=2`, `sif_rpc_call=2` unchanged). The matrix
   row appears not yet reconciled. PR #69 corrected the `PollSema` return value;
   `docs/ROADMAP.md` requires re-running the exact-title probe after that
   correction before naming the next frontier, so the ADR 0119 trace is the
   latest **executed** result and the post-correction re-run is pending. The
   product layer presents ADR 0119/PR #68 as current and treats
   `0x0025A724`/`semaphore_wait_blocked` as historical.
2. **Playable-title minimum.** `docs/RELEASE_GATES.md` (public beta) requires
   **≥3** titles through gameplay acceptance plans; `docs/ROADMAP.md` M6/M7
   target **≥2**. The two documents disagree on the minimum.
3. **Run-report fields.** `engine/src/main.cpp` writes
   `rg.reps2.engine.run_report.v1`; the COP0 register fields (`cause`, `epc`,
   `cop0_status`, `badvaddr`) are emitted by the **generated native probe trace
   line** in `rust/frontend/src/native_build.rs` (and by the probe), not by the
   engine runner. Product docs and the prototype note this distinction.

**Consequences.** These remain visible for Session 1/owners to reconcile; the
product blueprint does not depend on resolving them, but any UI that displays the
frontier must read the artifact of record rather than a narrative doc.

**Status.** Recorded (open, not owned by Session 2).

---

## Cross-references

- Design system: `design-system/reps2/MASTER.md`, `tokens.json`, `tokens.css`
- State model: `COMPATIBILITY_UX_MODEL.md`
- IA: `INFORMATION_ARCHITECTURE.md`
- Architecture & integration: `APPLICATION_ARCHITECTURE.md`,
  `ENGINE_INTEGRATION_CONTRACT.md`
- Roadmap: `IMPLEMENTATION_ROADMAP.md`
