# REPS2 Product Definition

> Basis: `origin/main` `cf34cf2` (worktree HEAD), frontend documented as **v0.7.0**
> (`README.md`), engine `kEngineVersion 0.2.0`, `kEngineAbiVersion 2`
> (`engine/include/rg_reps2_engine/version.hpp`). Canonical state model:
> [`COMPATIBILITY_UX_MODEL.md`](COMPATIBILITY_UX_MODEL.md). Screen IDs: S1–S13 in
> [`INFORMATION_ARCHITECTURE.md`](INFORMATION_ARCHITECTURE.md). Requirements: `PR-###`
> in [`PRODUCT_REQUIREMENTS.md`](PRODUCT_REQUIREMENTS.md). Journeys: `J1–J7` in
> [`USER_JOURNEYS.md`](USER_JOURNEYS.md). Visual system: `design-system/reps2/MASTER.md`.

This document fixes **what REPS2 is, who it is for, and what "done" means**. It does not
describe implementation detail, and it never promotes proposed work to existing capability.
Every claim below is classified `[E]` existing · `[P]` partial · `[PROP]` proposed ·
`[N]` not implemented · `[UNKNOWN]` no evidence available.

---

## 1. Product identity

**REPS2 is a Windows x64 platform that statically recompiles and natively executes
multiple PlayStation 2 titles from one owned ISO each, on one shared engine.**

The consumer surface is a library/launcher and developer console ("Studio Dark",
`design-system/reps2/MASTER.md` §1). The engine underneath is a title-agnostic static
AOT recompiler plus a shared PS2 runtime. The two are the *same product*: the library
only ever reports what the engine's artifacts prove.

| REPS2 **is** | REPS2 **is not** |
|---|---|
| A static AOT recompiler: R5900 → typed IR → C++20 → native module `[P]` | An interpreter-only emulator; no interpreter path is the strategy (`docs/ROADMAP.md:23`) |
| One shared runtime ABI across titles (`kEngineAbiVersion 2`) `[E]` | A per-game hand-written recompiler or forked instruction semantics |
| A truthful compatibility library that distinguishes 6 evidence-gated states `[P]` | A "built = playable" launcher that infers success from a compile or a boot logo |
| ISO-in, library-entry-out; no manual `BOOT.ELF` extraction `[E]` import, `[PROP]` orchestration | A ROM manager, a BIOS installer, or a file-extraction utility |
| Offline-first; no proprietary firmware required by the intended UX `[PROP]` goal | A scraper-dependent frontend; a distributor of Sony firmware or game assets |
| A developer workbench (`reps2 dev …`, `rg-reps2-engine`) `[E]` | A closed "black box" game launcher with no diagnostics |

The product is **one architecture, many titles**. A single title (Downhill Domination
USA, `SCUS_971.77`) is the current vertical-slice *test target*, never the architecture
(`docs/PRODUCT_VISION.md:18`, `docs/ROADMAP.md:21`).

---

## 2. Target user segments

Two personas. They share one binary, one library, one engine — but they ask different
questions of it, and the product must be honest to both.

### Persona A — "The Player" (consumer)

- **Situation:** owns PS2 discs/ISOs; wants them running natively on Windows PC with a
  controller, Big Picture-style, without engineering knowledge.
- **Only required input:** one lawfully owned ISO. No `BOOT.ELF`, no folders, no BIOS,
  no CLI.
- **Wants to see:** a game in a library, a truthful status, and a **Play** button that
  is enabled only when Play actually works.
- **Tolerance for partial states:** high, if labelled honestly; **zero** for a faked
  success. A title that is `Native-built` but `EXECUTION_BLOCKED` must say so and offer
  the next useful action, not a Play button (`COMPATIBILITY_UX_MODEL.md` §4).
- **Surfaces:** S1 Library, S2 empty, S3 Add Game, S4 Import & Build, S5 Game Detail,
  S6 Compatibility, S8 Execution/Session, S11 Settings, S12 Error & Recovery, S13 palette.
- **Success for this persona:** at least one owned title reaches `Playable` under a
  documented gameplay plan (`docs/RELEASE_GATES.md`).

### Persona B — "The Platform Engineer / QA" (the REPS2 developer using REPS2)

- **Situation:** builds/triages the recompiler and runtime; needs evidence, not vibes.
- **Wants to see:** which frontend stage or engine stop boundary blocked a title, with
  exact PC, syscall number, register state, digests, and a reproducible invocation.
- **Consumes:** `reps2 --dev` sessions, the `dev` namespace
  (`sessions|inspect|summary|compare|capabilities`), `rg-reps2-engine` run reports
  (`rg.reps2.engine.run_report.v1`), build manifests, native build identity JSON
  (`docs/CLI.md` §"Developer diagnostics", §"Agent and IDE developer CLI").
- **Must never be given:** a fabricated progress percentage, a per-instruction UI
  progress claim (`docs/CLI.md:112`), or a green check without a reproducible trace.
- **Surfaces:** S9 Developer Center, S10 Execution report, S7 Build Queue, S5/S6, S11.
- **Success for this persona:** any observed guest-visible state transition is
  reproducible from recorded artifacts, and `dev compare` shows a deterministic failure
  delta between revisions (`docs/CLI.md:143`).

---

## 3. The core promise

> **One owned ISO is the user's only required game input. REPS2 identifies the disc,
> extracts the boot ELF internally, analyzes, recompiles, builds a native module
> against one shared runtime, and registers the title in a persistent library with a
> truthful status — then runs it when the evidence permits.**

The promise is deliberately split into two halves, because they are at very different
maturity levels:

1. **Import + preparation half** — `[E]`: `reps2 add-game` already writes a
   content-addressed record (schema v1) with `playable:false` and
   `playability_status: "blocked_runtime_incomplete"`, referencing the source ISO by
   canonical path and never copying it (`rust/frontend/src/library.rs`, ADR 0010,
   `docs/CLI.md:151`).
2. **Orchestration + execution half** — `[PROP]`/`[N]`: the persistent library service
   (list/remove/relink/rescan/cache), Play/Stop, the persistent build queue, the
   settings store, and the scheduler **do not exist yet** (`docs/ROADMAP.md` M7). The UI
   must render this as proposed, never as current.

The promise is only kept when the second half is backed by the first half's artifacts.
There is no UI-only state (`COMPATIBILITY_UX_MODEL.md` §7).

---

## 4. Scope boundaries

### In scope (evidence-backed today)

- ISO 9660 (raw 2048-byte-sector) inspection and `SYSTEM.CNF` `BOOT2`/`BOOT` resolution `[E]`.
- Defensive ELF32 LE parsing, PT_LOAD materialization, 32 MiB initial RAM image `[E]`.
- Partial R5900 decode, reachability-guided CFG, typed IR, deterministic C++20 emit `[E]`.
- Bounded native build (`--native-build`, `recompile-iso`) and bounded host runner `[E]`.
- One-ISO managed import and a content-addressed per-game manifest `[E]`.
- Session-scoped developer diagnostics and an agent-readable `dev` namespace `[E]`.

### In scope, proposed (do not present as existing)

- Persistent multi-game **library service** (add/remove/relink/rescan/integrity) `[PROP]`.
- **Build queue** with resumable jobs and per-game cache reuse `[PROP]`.
- **Settings store** (library paths, runtime defaults, logging, dev mode) `[N]`.
- **Play/Stop** session manager and per-game settings/saves/logs `[PROP]`.
- Compatibility-derivation layer that turns artifacts into S1–S13 states `[PROP]`.
- Clean-room HLE boot/kernel behavior so no proprietary BIOS is needed `[PROP]`.

### Explicitly out of scope (adjacent problems, not this product)

- Raw 2352-byte BIN/CUE, CHD, UDF, multi-volume, PFS/HDD, Joliet/Rock Ridge `[N]`
  (`docs/CLI.md:155`).
- Android/ARM64 delivery today (design target only; deferred until Windows x64 is
  complete, `docs/ROADMAP.md:65`).
- Network metadata/cover scraping as a hard dependency; the library must work offline
  (`INFORMATION_ARCHITECTURE.md` §5).
- Signed/hot-unloadable third-party module marketplace `[N]` (`docs/ROADMAP.md:66`).

---

## 5. Non-goals

1. **No fake success.** No zero-read for an unsupported device, no no-op write, no
   fabricated syscall result, no invented boot stage (`AGENTS.md`; `COMPATIBILITY_UX_MODEL.md` §3).
2. **No exit-code-as-success.** A zero process exit means a bounded run produced a
   report, not that a game booted (`docs/CLI.md:75`).
3. **No per-title forks.** Title-specific workarounds are isolated, documented, tested,
   and never fork shared instruction or device semantics (`docs/PRODUCT_VISION.md:20`).
4. **No fabricated progress.** Coarse pipeline stages are shown as stages, never as
   per-instruction percentages (`docs/CLI.md:112`).
5. **No copyrighted material in-repo.** No ISO, `BOOT.ELF`, BIOS, proprietary SDK, game
   art, or copied third-party implementation code (`AGENTS.md`).
6. **No state the backend cannot prove.** If a stage's evidence artifact is absent or
   unparseable, the state must not be promoted (`COMPATIBILITY_UX_MODEL.md` §1).
7. **No engine telemetry invented by the UI.** The product layer reads only versioned
   artifacts; it derives, it does not invent (`COMPATIBILITY_UX_MODEL.md` §1).

---

## 6. Success definition (tied to `docs/RELEASE_GATES.md`)

Success is defined at the *gate* level and at the *capability-state* level. The two must
agree: a gate is met only when the corresponding real-title evidence exists.

| Gate (`RELEASE_GATES.md`) | Requires | Capability-state consequence | Today |
|---|---|---|---|
| **Alpha** | ISO/ELF inspection + stable manifests; end-to-end analysis→native generation→documented native runtime execution; structured diagnostics for unsupported behavior; reproducible tests | ≥1 title reaches `Native-built`; unsupported runtime behavior surfaces as a named blocker | **Partially met**: DD is `Native-built`; no title `Boot-tested` `[P]` |
| **Public beta candidate** | Installable Windows x64 app; one-ISO import; **≥3 distinct titles through gameplay acceptance plans**; regression coverage; recovery/diagnostics/reproducible build | ≥3 titles `Playable`; S1/S3/S5/S11/S12 complete | **Not met** `[N]` |
| **REPS2 1.0** | Beta criteria + evidence-backed multi-game matrix + no critical correctness defects + services match matrix + complete UX/controller/packaging/clean-install/security/perf/licensing/independent audit | Catalog across states; ≥2 independently validated `Playable` (M6/M7 targets) | **Not met** `[N]` |

Lifecycle order (forward-only, evidence-gated):
`Imported → Analyzed → Translated → Native-built → Boot-tested → Playable`.
A later state is **never** implied by an earlier one. Compiling ≠ playable; boot logo ≠
playable (`docs/PRODUCT_VISION.md:86`, `COMPATIBILITY_UX_MODEL.md` §2).

---

## 7. Reality ledger (must be reflected honestly by every screen)

| Fact | Value | Class |
|---|---|---|
| Repository head at basis | `cf34cf2` | Observed |
| Frontend documentation version | v0.7.0 (`README.md`) | Observed |
| Engine / ABI | `0.2.0` / ABI `2` | Observed |
| Titles `Boot-tested` | **none** | Observed |
| Titles `Playable` | **none** | Observed |
| Downhill Domination USA `SCUS_971.77` | `Native-built`; latest executed probe stops `budget_exhausted` in a `PollSema` (0x45) poll loop at `pc=0x00246DAC` after 4,999,998 instructions (ADR 0119 / PR #68 in `docs/PROJECT_STATE.md`; the ADR 0101 `semaphore_wait_blocked` @ `0x0025A724` recorded in `docs/COMPATIBILITY_MATRIX.md:7` is historical) | Observed |
| Black USA `SLUS_213.76` | historical, `NOT TESTED` after later runtime changes (`docs/COMPATIBILITY_MATRIX.md:8,10`) | Observed |
| Library import | exists; layout `<library>/games/<sha256>/{game.json,boot.elf,recompiled.cpp,ee-main-ram.bin}`; default `%LOCALAPPDATA%\REPS2\Library` | Observed |
| Library service / Play / queue / settings / scheduler | absent | Observed |

Any screen that would imply a `Boot-tested` or `Playable` title today is a **defect**
against this ledger.

---

## 8. Evidence discipline & capability classification

Keep four categories distinct (`AGENTS.md`): **Observed** (exact trace/bytes/digest),
**Derived** (deterministic conclusion from observations), **Inferred** (hypothesis),
**Unknown**. Apply the five capability classes to every claim
(`[E]`/`[P]`/`[PROP]`/`[N]`/`[UNKNOWN]`). The product surface rules that follow:

- The UI derives state from **one** tuple:
  `(stage, evidence_refs[], blockers[], allowed_actions[], disabled_actions[], recovery[])`
  (`COMPATIBILITY_UX_MODEL.md` §1).
- `Unknown` is first-class: absent artifacts never promote a stage.
- Disabled actions are visible with a reason (WCAG `empty-nav-state`).

---

## 9. Cross-references

- State model: [`COMPATIBILITY_UX_MODEL.md`](COMPATIBILITY_UX_MODEL.md)
- Screens/nav: [`INFORMATION_ARCHITECTURE.md`](INFORMATION_ARCHITECTURE.md)
- Requirements: [`PRODUCT_REQUIREMENTS.md`](PRODUCT_REQUIREMENTS.md)
- Journeys: [`USER_JOURNEYS.md`](USER_JOURNEYS.md)
- Source of truth (repo): `docs/PRODUCT_VISION.md`, `docs/ROADMAP.md`,
  `docs/RELEASE_GATES.md`, `docs/CAPABILITIES.md`, `docs/COMPATIBILITY_MATRIX.md`,
  `docs/CLI.md`, `docs/PROJECT_STATE.md`, `docs/decisions/` (ADRs), `AGENTS.md`.
