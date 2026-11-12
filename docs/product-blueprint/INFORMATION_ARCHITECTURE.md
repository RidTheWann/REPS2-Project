# REPS2 Information Architecture

Navigation model, screen inventory, and the mapping from screens to backend
interfaces. Detailed per-screen behaviour lives in `SCREEN_SPECIFICATIONS.md`;
the state vocabulary lives in `COMPATIBILITY_UX_MODEL.md`.

---

## 1. Primary shape

A **Windows desktop shell** with four persistent regions. There is no
web-style page router; navigation is a **contextual two-level model** (top-level
destination + per-screen context) so it maps cleanly onto both keyboard and
controller.

```
┌──────────────────────────────────────────────────────────────────────┐
│ Top command bar (56px)  logo · search · global status · window/account │
├──────────────┬─────────────────────────────────────────┬─────────────┤
│ Left nav     │  Content region                          │ Right        │
│ rail (240)   │  (library grid / detail / pipeline /     │ inspector    │
│ nav + library│  developer console / settings)           │ (360, ctx)   │
├──────────────┴─────────────────────────────────────────┴─────────────┤
│ Status bar: library counts · engine ABI · active jobs · notifications │
└──────────────────────────────────────────────────────────────────────┘
```

- **Top-level destinations** are in the left rail: `Library`, `Add Game`,
  `Build Queue`, `Developer`, `Settings`.
- **Game Detail** is a context screen reached from Library (not a rail item).
- **Execution / Session** is a context screen reached from a game.
- The right **inspector** is contextual: selection info, job detail, or
  diagnostic detail. It can be collapsed.

### Controller / keyboard model

- One focus ring, one focus order, top-to-bottom then left-to-right.
- D-pad / arrows move focus; A/Enter activates; B/Esc goes back one level.
- `LB/RB` switch top-level destinations; `Y` toggles the inspector; `X` opens
  the command palette / search.
- All of this is the *same* focus model as keyboard — no controller-only paths.

---

## 2. Screen inventory

| ID | Screen | Rail item | Purpose | Primary action | Reachable states |
|---|---|---|---|---|---|
| S1 | **Library** | ✓ | Browse/filter/sort owned games with truthful status | Open game / Play (if Playable) | all library states, empty, loading |
| S2 | **Library (empty)** | (S1 state) | First-run onboarding | Add Game | empty |
| S3 | **Add Game dialog** | ✓ | Select one owned ISO and run the import pipeline | Select ISO → Import | idle, validating, duplicate, error |
| S4 | **Import & Build workflow** | (S3 continuation) | Transparent pipeline stages for the new title | Cancel / Open job | per-stage pending/running/ok/failed |
| S5 | **Game Detail** | – | Identity, evidence, limitations, actions for one title | next valid action | all lifecycle + failure states |
| S6 | **Compatibility view** | (S5 tab) | Lifecycle pill history + evidence ledger | – | all lifecycle states |
| S7 | **Build Queue** | ✓ | All in-flight and recent jobs across titles | Open job | idle, running, done, failed |
| S8 | **Execution / Session** | – | Run a bounded session; show stop reason + live counters | Play / Stop (per state) | pre-run, running, blocked, crashed, ended |
| S9 | **Developer Center** | ✓ | Engine/analysis/build diagnostics index | Open report | populated, empty |
| S10 | **Execution report** | (S9 detail) | One session: stop status, PC, regs, syscalls, devices | Export | ok, blocked, error |
| S11 | **Settings** | ✓ | Library paths, build storage, runtime, input, logging, dev mode | Save | defaults, modified, invalid |
| S12 | **Error & Recovery** | (S1/S5 state) | Failed stage + cause + recovery | Retry / Relink / Export | failure variants |
| S13 | **Command palette** | (overlay) | Keyboard/controller-driven action jump | run action | – |

Additional overlays (not full screens): **notifications/toasts**, **confirm
dialogs** (destructive actions), **about/version**.

---

## 3. Navigation graph

```
Library ──select──▶ Game Detail ──tab──▶ Compatibility
   │                    │
   │                    ├──action──▶ Execution / Session ──▶ Execution report
   │                    └──action──▶ Build (job) ──▶ Build Queue
   │
Add Game ──import──▶ Import & Build workflow ──done──▶ Game Detail
Build Queue ──open──▶ (Job detail in inspector)
Developer ──open──▶ Execution report
Settings (modal-less destination)
Any failure ──▶ Error & Recovery (inline or dialog)
```

Back navigation restores the previous screen including scroll position, filter,
and selection (`state-preservation`).

---

## 4. Screen → backend interface mapping

Classification: **E** = existing, **P** = partially implemented, **N** = not
implemented, **PROP** = proposed. See `ENGINE_INTEGRATION_CONTRACT.md`.

| Screen | Data source | Class |
|---|---|---|
| S1 Library | library directory index (`%LOCALAPPDATA%\REPS2\Library`), `game.json` per game | P (import exists; no list/index API) |
| S3 Add Game | `reps2 add-game <iso> [--library]` | E |
| S4 Import & Build | `add-game` stdout/`--dev` events; `recompile-elf`; generated CMake+native build; identity JSON | E (commands) / P (streamed stage API) |
| S5 Game Detail | `game.json` + `<stem>.manifest.json` + identity JSON | P |
| S6 Compatibility | derived from `game.json`/manifest/run-report per §COMPATIBILITY_UX_MODEL | PROP (derivation layer) |
| S7 Build Queue | job manager (no persistent queue exists today) | PROP |
| S8 Execution | `rg-reps2-engine <module> [--ram|--media|--budget|--hle-syscalls]` | E (runner) / P (session control) |
| S9 Developer Center | `reps2 dev sessions|inspect|summary|compare|capabilities` | E |
| S10 Execution report | engine run report `rg.reps2.engine.run_report.v1` + developer report v1 | E |
| S11 Settings | app config file (none today), library root, runtime defaults | N |
| S12 Error & Recovery | command failure events + run-report `status` | P |
| S13 Command palette | action registry over the above | PROP |

No screen calls Rust recompiler logic or C++ engine internals directly. All
screens go through application services that own the subprocess/JSON boundary
(`APPLICATION_ARCHITECTURE.md`).

---

## 5. Global states (apply to every screen)

- **Loading:** skeleton for unknown duration; determinate bar only when a real
  fraction exists.
- **Empty:** icon + explanation + primary action; never a blank region.
- **Error:** cause + recovery in-place (`role="alert"`), plus a link to the
  structured report.
- **Offline/permission:** library and engine work entirely offline; no screen
  may require network. If metadata enrichment is ever added it is optional.

---

## 6. Terminology (shared vocabulary)

Use these exact terms in UI copy so the product, CLI, and docs stay aligned:

`ISO` · `disc` · `boot ELF` · `analysis` · `CFG` · `IR` · `translation unit` ·
`native module` · `shared runtime` · `engine` · `RG REPS2 ENGINE` ·
`compatibility state` · `execution session` · `bounded execution` ·
`unsupported instruction` · `unresolved control flow` · `runtime service`.

Avoid: "emulator", "ROM", "playable = built", "boot logo = working".
