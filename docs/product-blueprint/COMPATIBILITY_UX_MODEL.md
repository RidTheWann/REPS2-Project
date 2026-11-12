# REPS2 Compatibility & Application State Model

This is the canonical state model. Every screen, action, and API field in the
product blueprint derives from it. It deliberately extends the lifecycle already
published in `docs/PRODUCT_VISION.md` (lines 79-86) and `AGENTS.md`, and it is
bound to **actual backend evidence**, not to UI optimism.

Guiding invariant (from `docs/PRODUCT_VISION.md:86` and `AGENTS.md`):

> A previous stage never implies a later one. Parsing an ISO, generating C++,
> compiling a host executable, or reaching a boot logo/menu does not alone mean
> a title is Playable.

---

## 1. Evidence sources (what the UI is allowed to read)

The product layer does not invent telemetry. It reads only these versioned
artifacts and derives the state from them:

| Artifact | Producer | Kind / schema | Evidence class |
|---|---|---|---|
| `game.json` | `reps2 add-game` (`rust/frontend/src/library.rs`) | library manifest, schema v1; carries `playable:false`, `playability_status` | Observed |
| `<stem>.manifest.json` | `reps2 recompile-elf` (`rust/frontend/src/build_manifest.rs`) | build manifest schema v3; `playable:false` | Observed |
| `reps2-native-build-identity-<config>.json` | generated CMake project (`rust/frontend/src/native_build.rs`) | `cache_key_sha256` + toolchain report | Observed |
| engine run report | `rg-reps2-engine` (`engine/src/main.cpp`) | `rg.reps2.engine.run_report.v1` on stdout; exit codes 2/3/4/5/6/10 | Observed |
| developer report | `reps2 dev …` (`rust/frontend/src/developer.rs`) | `developer-report-v1.schema.json` | Observed |
| guest stop status | runtime (`runtime/include/reps2/runtime/ee_state.hpp`) | e.g. `unhandled_syscall`, `budget_exhausted`, `guest_exception`, `semaphore_wait_blocked`, `unresolved_control_flow` | Observed |

**Machine-readable state meaning** = the tuple
`(stage, evidence_refs[], blockers[], allowed_actions[], disabled_actions[], recovery[])`
derived from the artifacts above. "Unknown" is a first-class value: if an
artifact needed for a stage is absent or unparseable, the state must not be
promoted.

---

## 2. Primary lifecycle states (forward-only)

| # | State | Machine meaning | Evidence required to ENTER |
|---|---|---|---|
| 1 | **Imported** | Disc validated, boot ELF extracted and stored; no analysis evidence | `game.json` exists with valid ISO SHA-256 + boot ELF SHA-256 |
| 2 | **Analyzed** | CFG/analysis coverage recorded for the boot image | build/analysis manifest with block/instruction/root counts for the exact boot ELF digest |
| 3 | **Translated** | A documented subset emitted typed IR → C++ (possibly with unsupported/unresolved items) | generated `.cpp` + manifest schema recorded; `playable:false` |
| 4 | **Native-built** | Generated code compiled and linked against a specific runtime ABI | native build identity `cache_key_sha256` present + successful link |
| 5 | **Boot-tested** | Repeatable, precisely documented **guest-visible boot stage** reached | engine run report for the exact module with a repeatable stage, non-`budget_exhausted` outcome, recorded digests + config |
| 6 | **Playable** | Documented representative gameplay acceptance plan passes | boot-tested evidence **plus** a gameplay plan covering rendering, input, audio, session stability, saves |

**Current reality (2026-11-10, `origin/main cf34cf2`):** no title is
`Boot-tested` or `Playable` (`docs/COMPATIBILITY_MATRIX.md`). Downhill
Domination USA reaches `Native-built`; its latest executed probe stops
`budget_exhausted` in a `PollSema` (0x45) poll loop at `pc=0x00246DAC` after
4,999,998 instructions (ADR 0119 / PR #68 — the ADR 0101
`semaphore_wait_blocked` at `0x0025A724` is historical, and the post-PR-#69
probe re-run is pending). Black USA is historical/`NOT TESTED`. The UI must
render exactly this.

---

## 3. Interrupt / failure states

These are entered when a stage fails or is blocked. Each maps to a real backend
signal, never to a guess.

| State | Machine meaning | Backend signal | Recovery |
|---|---|---|---|
| **Import failed** | ISO unreadable/unsupported layout or boot ELF invalid | `reps2` non-zero exit; command failure event `REPS2_COMMAND_FAILED` | Re-select ISO; show exact reason (raw 2352/BIN-CUE/CHD/UDF unsupported) |
| **Analysis failed** | Analysis limits exceeded or ELF malformed | pipeline stage `pipeline.stage.failed` (e.g. `cfg.discover`, `ir.lift`), named limit flag | Raise the named limit (`--max-blocks` etc.) or accept partial |
| **Translation incomplete** | Some reachable blocks unsupported/unresolved | manifest unsupported-instruction / unresolved-target counts (> 0) | Expected for real titles; disclose counts, do not block the stage |
| **Native build failed** | Host compile/link failed | CMake/MSVC error; identity file absent | Show compiler diagnostics (stderr), retry, allow export of the generated project |
| **Execution blocked** | Module built but runtime stops inside guest code | engine run report `status` ≠ success (e.g. `semaphore_wait_blocked`, `unhandled_syscall`) + stop PC | Surface the exact stop + blocking service; no Play available |
| **Unsupported** | A required device/service has no implementation | `unhandled_syscall` / `unsupported_instruction` / `unresolved_control_flow` stop PC + identifier | Report as a runtime requirement; feeds the backlog, never faked |
| **Recovery required** | Referenced source ISO moved/digest mismatch, or library record inconsistent | `game.json` canonical path missing, or integrity re-check failed | Offer relink to the same digest; never silently rebind to a different disc |

---

## 4. Action availability matrix

"Play" is the only action that requires `Playable`. Build/test actions appear
progressively. Disabled actions must still be visible with a reason (WCAG
`empty-nav-state`: explain, do not silently hide).

| Action | Imported | Analyzed | Translated | Native-built | Boot-tested | Playable |
|---|---|---|---|---|---|---|
| View details | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Analyze / re-analyze | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Translate / re-translate | – | ✓ | ✓ | ✓ | ✓ | ✓ |
| Native build | – | – | ✓ | ✓ | ✓ | ✓ |
| Run bounded execution probe | – | – | – | ✓ | ✓ | ✓ |
| **Play** | – | – | – | – | – (test only) | ✓ |
| View execution report | – | – | – | ✓ | ✓ | ✓ |
| Relink source ISO | ✓* | ✓* | ✓* | ✓* | ✓* | ✓* |

✓* = available whenever the source path is stale, i.e. `Recovery required`.

Any stage that failed suppresses actions strictly downstream of it (e.g.
`Native build failed` disables Play and Run) — the UI must not offer an action
whose precondition evidence is missing.

---

## 5. Transition rules (guarded)

```
Imported --analyze(ok)--> Analyzed
Analyzed --translate(ok)--> Translated
Translated --native-build(ok)--> Native-built
Native-built --run(repeatable guest-visible stage)--> Boot-tested
Boot-tested --gameplay-plan(pass)--> Playable

any --stage-failure--> <stage>_FAILED / <stage>_INCOMPLETE
Native-built --run(non-success stop)--> EXECUTION_BLOCKED
<built+> --source-missing/--digest-mismatch--> RECOVERY_REQUIRED
RECOVERY_REQUIRED --relink(same digest, ok)--> previous stable state
```

Rules:

1. **Promotion requires evidence**, verified against exact digests. A stage is
   never promoted by the mere existence of a process that exited 0
   (`AGENTS.md` explicitly forbids exit-code-as-success).
2. **Downgrade is explicit.** If re-import or a new toolchain invalidates
   artifacts, the state regresses to the highest stage still backed by evidence.
3. **`Translation incomplete` is not a failure.** It is recorded alongside a
   forward state; unsupported counts are disclosed, not hidden.
4. **`Playable` is never inferred from `Native-built` or a boot logo.**

---

## 6. Visual vocabulary (see `design-system/reps2/MASTER.md` §8)

Every state renders as: **status pill** (icon + label + status token) +
optional one-line evidence summary + a tooltip stating the exact evidence
requirement. Colour is never the only signal.

- `Imported` — neutral, box icon, "Disc and boot ELF validated".
- `Analyzed` — info, scan icon, "<blocks> blocks · <insns> instructions".
- `Translated` — progress, code icon, "N unsupported · M unresolved".
- `Native-built` — built, cpu icon, "linked · ABI <ver> · <cache_key prefix>".
- `Boot-tested` — test, power icon, "reached <stage> @ <PC>".
- `Playable` — playable, play icon, "gameplay plan <id> passed".
- Failure/blocked states — danger/blocked tokens with the exact cause + recovery.

`Play` is rendered **enabled only in `Playable`**; in `Boot-tested` it is
replaced by "Run test session"; in earlier states by the next valid action.

---

## 7. Non-goals

- No fabricated progress percentages for stages the backend reports as
  coarse-grained only (`docs/CLI.md` states stage attribution is a frontend
  stage, not per-instruction).
- No state that exists only in the UI and cannot be reproduced from artifacts.
- No silent success for unimplemented runtime/device behavior.
