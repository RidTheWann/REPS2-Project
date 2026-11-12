# REPS2 Product Blueprint

Session 2 product-definition, UX, design-system, and prototype artifacts for
REPS2 — a Windows x64 PlayStation 2 **static AOT recompilation platform** whose
engine is **RG REPS2 ENGINE**. This directory contains **product and design**
material only; it does not modify the engine, recompiler, or runtime, and it
makes no compatibility claim beyond `docs/COMPATIBILITY_MATRIX.md`.

## Contents

| Document | Purpose |
|---|---|
| [`PRODUCT_DEFINITION.md`](PRODUCT_DEFINITION.md) | Identity, personas, scope, non-goals, success definition |
| [`PRODUCT_REQUIREMENTS.md`](PRODUCT_REQUIREMENTS.md) | Numbered requirements (PR-###) with layer/class/verification/failure-UX |
| [`USER_JOURNEYS.md`](USER_JOURNEYS.md) | End-to-end journeys J1–J7 mapping to screens |
| [`INFORMATION_ARCHITECTURE.md`](INFORMATION_ARCHITECTURE.md) | Navigation model, screen inventory S1–S13, screen→backend map |
| [`SCREEN_SPECIFICATIONS.md`](SCREEN_SPECIFICATIONS.md) | Per-screen layout, states, focus order, gated actions, data sources |
| [`COMPATIBILITY_UX_MODEL.md`](COMPATIBILITY_UX_MODEL.md) | Canonical lifecycle + failure states, evidence rules, action matrix |
| [`ACCESSIBILITY_AND_INTERACTION.md`](ACCESSIBILITY_AND_INTERACTION.md) | Focus/keyboard/controller model, contrast, motion, validation status |
| [`APPLICATION_ARCHITECTURE.md`](APPLICATION_ARCHITECTURE.md) | Layers, services, process model, persistence, GUI technology decision |
| [`ENGINE_INTEGRATION_CONTRACT.md`](ENGINE_INTEGRATION_CONTRACT.md) | Exact subprocess/JSON contracts with exit-code taxonomy |
| [`IMPLEMENTATION_ROADMAP.md`](IMPLEMENTATION_ROADMAP.md) | Phases 0–4 from current reality to the M7 gate |
| [`DESIGN_DECISIONS.md`](DESIGN_DECISIONS.md) | Product/design decisions DD-001…011 and observed repo inconsistencies |

## Companion artifacts

- **Design system (source of truth):** [`../../design-system/reps2/MASTER.md`](../../design-system/reps2/MASTER.md), `tokens.json`, `tokens.css`
- **Interactive prototype:** [`../../product-prototype/`](../../product-prototype/) — open `index.html`, or `python -m http.server 8080 --directory product-prototype`
- **Screenshots:** [`screenshots/`](screenshots/)

## Read in this order

1. `PRODUCT_DEFINITION.md` → `COMPATIBILITY_UX_MODEL.md` (the truth model)
2. `INFORMATION_ARCHITECTURE.md` → `SCREEN_SPECIFICATIONS.md`
3. `APPLICATION_ARCHITECTURE.md` → `ENGINE_INTEGRATION_CONTRACT.md`
4. `DESIGN_DECISIONS.md` (why the above is the way it is)

## Status

`Playable` is **not** available for any title; `Boot-tested` is not yet reached.
The blueprint is implementation-ready but gated behind the M4/M5 runtime work in
`IMPLEMENTATION_ROADMAP.md`. The interactive prototype is a design-validation
artifact, **not** the production GUI.
