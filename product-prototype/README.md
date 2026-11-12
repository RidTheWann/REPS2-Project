# REPS2 Interactive Product Prototype

A **design-validation artifact** for the intended REPS2 Windows desktop
experience. It is **not** the production GUI and it is **not** connected to the
Rust frontend or the RG REPS2 ENGINE. Every simulated action is labelled as a
prototype demonstration; nothing here imports an ISO or executes a PS2 game.

## Run it

No build step and no dependencies. Open the file directly:

```
start product-prototype\index.html          # Windows (cmd/PowerShell)
```

or serve it (keeps relative paths clean):

```
python -m http.server 8080 --directory product-prototype
# then browse http://localhost:8080/
```

From the repository root the entry point is `product-prototype/index.html`.

## Smoke test

`tests/smoke.html` loads the real `data.js` + `app.js` and runs 15 interaction
assertions (routing, filtering, search focus, action gating, dialog focus
restore, toast, empty state, stage set). No dependencies — run it in any
headless Chromium and read the `SMOKE:` summary:

```
chrome --headless=new --disable-gpu --no-sandbox --dump-dom \
  "file:///<repo>/product-prototype/tests/smoke.html" | grep SMOKE
```

Expected: `SMOKE: PASS 15/15`. The test was verified to fail (14/15) when the
Play action is deliberately un-gated, so it guards the lifecycle contract.

## What is implemented

| Screen (IA id) | Interaction |
|---|---|
| S1 Library | grid/list toggle, search, sort, filter by compatibility state, selection + inspector |
| S2 Empty library | shown when filters exclude everything, with a clear-filter action |
| S3 Add Game | dialog with path input, dropzone, validation copy (simulated import) |
| S4 Import & Build | animated stage stepper + log well; "Simulate pipeline" is a demo |
| S5 Game Detail | hero, gated action set, tabs across Overview/Compatibility/Evidence/Limitations |
| S6 Compatibility | lifecycle stepper with evidence requirements per stage |
| S7 Build Queue | job table (running/succeeded/failed) |
| S8 Execution | reached via Run bounded session |
| S9 Developer Center | session table, `dev capabilities` JSON view |
| S10 Execution report | run-report fields, COP0 state, syscall list, illustrative chart |
| S11 Settings | paths, runtime toggles, developer mode, future controls labelled |
| S12 Error & Recovery | build-failure dialog, relink dialog, structured failure event |
| S13 Command palette | Ctrl/Cmd-K placeholder |

Interactions are real: navigation, selection, search, filtering, dialogs
(focus-trapped, Escape to close), tabs, toggles, and the pipeline simulation.
Responsive from 1024×720 up to 3840×2160.

## Design system

All styling consumes `../design-system/reps2/tokens.css` — the single source of
truth defined by `../design-system/reps2/MASTER.md`. No raw hex values are used
in components.

## What is simulated vs real

- **Simulated:** all library records, jobs, sessions, run reports, and the
  pipeline advance. The two documented real titles (SCUS_971.77, SLUS_213.76)
  carry their **true** status from `docs/COMPATIBILITY_MATRIX.md`; the other
  entries are clearly labelled synthetic samples.
- **Real:** the lifecycle model, action-gating rules, token system, contrast
  hierarchy, and interaction patterns that the production app must implement.

## Known limitations (honest)

- Screenshots were captured with headless Chrome (see
  `../docs/product-blueprint/screenshots/`); this session could not display
  images inline, so screens were verified by asserting expected DOM markers per
  route rather than by visual inspection.
- The prototype is a single HTML page with vanilla JS; the production shell is
  expected to be **WinUI 3** (see `../docs/product-blueprint/APPLICATION_ARCHITECTURE.md`).
- Rendered styles depend on the Google-hosted Inter/JetBrains Mono fonts; the
  token fallbacks (system-ui / Segoe UI / Consolas) degrade gracefully offline.
- `color-mix()` is used for status-chip borders, which requires a modern
  Chromium/WebView2/Firefox engine (fine for the intended WinUI 3 host).
