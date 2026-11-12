# REPS2 Design System — Master (Source of Truth)

> Retrieval rule: when building a specific screen, first check
> `design-system/reps2/pages/<screen-name>.md`. If that file exists its rules
> **override** this master. Otherwise follow this file exactly.

- **Project:** REPS2 — Windows-native PlayStation 2 static AOT recompilation platform
- **Surface:** Windows x64 desktop application (library / launcher / developer console)
- **Version:** 1.0 (Session 2 product blueprint)
- **Machine-readable tokens:** `design-system/reps2/tokens.json`
- **Implementation tokens:** `design-system/reps2/tokens.css`

This master was seeded by the UI/UX Pro Max design-system generator
(`search.py --design-system --persist -p REPS2`) and then **deliberately
re-authored**, because the generator's generic output was light-oriented and
internally contradictory for a dark desktop product (a `#1E293B` secondary
button on a `#0F172A` background is invisible; `backdrop-filter` blurs and
pure-black surfaces hurt desktop readability). The generator's accessibility
foundations are retained. See
`docs/product-blueprint/DESIGN_DECISIONS.md` (DD-001).

---

## 1. Visual direction

**Chosen language: "Studio Dark" — a restrained, premium, technical dark UI.**

It blends the UI/UX Pro Max *Modern Dark (Cinema)* style direction with the
*Developer Tool / IDE* dark palette, because REPS2 is simultaneously a consumer
game library and a professional engineering console.

### Adopted from research

- **Style:** Modern Dark (Cinema) — deep neutral base, hairline borders,
  layered elevation, restrained accent, no pure `#000000` (avoids smearing and
  preserves surface separation). Source: `styles.csv` → "Modern Dark (Cinema)".
- **Palette family:** "Developer Tool / IDE" dark palette (code-dark surface
  family + a single “run” green accent). Source: `colors.csv` →
  "Developer Tool / IDE".
- **Type:** Inter UI + JetBrains Mono for technical data. Source:
  `typography.csv` → "Modern Dark Cinema" and "Developer Mono".
- **Charting:** line/area for execution trends, AA accessibility, differentiate
  series by line style not colour alone. Source: `charts.csv` → "Trend Over Time".

### Deliberately rejected (directive §5 anti-patterns)

Cyberpunk neon, HUD/FUI sci-fi glow, pink-heavy palettes, decorative particles,
uncontrolled glow, artificial HDR, glass-for-decoration, and card-in-card
nesting. These match the directive's forbidden list and fail contrast checks.

---

## 2. Colour tokens

Semantic tokens only. Components never reference raw hex. The full dark theme is
canonical; a light theme is optional and out of scope for v1.

### 2.1 Surfaces (deep → raised)

| Token | Hex | Usage |
|---|---|---|
| `--bg-deep` | `#070B14` | Window chrome, side rail, status bar |
| `--bg-base` | `#0B1120` | App background / content canvas |
| `--surface-1` | `#111A2E` | Panels, list backgrounds |
| `--surface-2` | `#16213A` | Cards, elevated tiles |
| `--surface-3` | `#1D2A47` | Hover / selected surface |
| `--surface-inset` | `#0A0F1C` | Inputs, code/console wells |

### 2.2 Borders & separators

| Token | Value | Usage |
|---|---|---|
| `--border-subtle` | `rgba(255,255,255,0.07)` | Hairline dividers, card edges |
| `--border-strong` | `rgba(255,255,255,0.14)` | Focused/raised edges, inputs |
| `--border-accent` | `rgba(110,139,255,0.45)` | Selected/active outlines |

### 2.3 Text

| Token | Hex | Min contrast target |
|---|---|---|
| `--text-primary` | `#E8EDF7` | ≥ 12:1 on `--bg-base` |
| `--text-secondary` | `#A7B4CC` | ≥ 7:1 on `--bg-base` |
| `--text-muted` | `#7E8DA8` | ≥ 4.5:1 on `--bg-base` (verify before use at < 14px) |
| `--text-disabled` | `#55617A` | Exempt (non-essential) |

### 2.4 Interactive / brand

| Token | Hex | Usage |
|---|---|---|
| `--brand` | `#6E8BFF` | Primary actions, selection, links, focus ring |
| `--brand-strong` | `#8AA2FF` | Hover/pressed on brand |
| `--brand-subtle` | `rgba(110,139,255,0.14)` | Selected row fill, tag background |
| `--on-brand` | `#0A0F1C` | Text/glyph on brand fills |

### 2.5 Execution / "run" accent

| Token | Hex | Usage |
|---|---|---|
| `--run` | `#22C55E` | **Play / Execute**, success emphasis |
| `--run-strong` | `#3DDC79` | Hover/pressed run |
| `--on-run` | `#04160B` | Text/glyph on run fills |

### 2.6 Semantic status colours

Reusable status tokens. **Every status is also labelled with text + an SVG icon;
colour alone never conveys meaning** (WCAG `color-not-only`).

| Token | Hex | Meaning |
|---|---|---|
| `--status-neutral` | `#7C8BA5` | Imported / idle / no evidence |
| `--status-info` | `#4F8CFF` | Analyzed / informational |
| `--status-progress` | `#8B7CFF` | Translated / in-progress build |
| `--status-built` | `#22B8CF` | Native-built (compiled, not validated) |
| `--status-test` | `#F5A623` | Boot-testing / partially verified |
| `--status-playable` | `#22C55E` | Playable (verified) |
| `--status-warn` | `#F5A623` | Warning / recovery required |
| `--status-danger` | `#EF4444` | Failed (import/analysis/build/exec) |
| `--status-blocked` | `#E5484D` | Execution blocked / unsupported |

All status fills are used as **text/border/icon** colours on a
`--surface-*`-derived chip background (14% alpha). Because a same-hue tint
lightens that background, pill labels use the lighter `--status-*-text` tokens
(§2.7), not the accent tokens above; measured chip-label contrast is 4.78–7.40:1.

### 2.7 Status text tokens (for chips)

`--status-neutral-text #AEB9CC` · `--status-info-text #7FAEFF` ·
`--status-progress-text #B4AAFF` · `--status-built-text #5FD3E6` ·
`--status-test-text #FFC46B` · `--status-playable-text #5FE08B` ·
`--status-warn-text #FFC46B` · `--status-danger-text #FF6B6B` ·
`--status-blocked-text #FF7075`. Use these for text/icons **on chip
backgrounds**; keep the §2.6 accent tokens for borders, charts and plain
surfaces.

---

## 3. Typography

- **UI / body / headings:** `Inter` (weights 400/500/600/700).
- **Technical data only:** `JetBrains Mono` (addresses, digests, hex, register
  values, PC, paths, console output, syscall numbers). Mono is **not** used for
  body prose.

| Role | Size / line-height | Weight | Font |
|---|---|---|---|
| Display | 32 / 1.2 | 700 | Inter (tracking -0.02em) |
| H1 | 24 / 1.25 | 600 | Inter |
| H2 | 20 / 1.3 | 600 | Inter |
| H3 | 16 / 1.4 | 600 | Inter |
| Body | 14 / 1.5 | 400 | Inter |
| Body-sm | 13 / 1.5 | 400 | Inter |
| Label | 12 / 1.4 | 500 | Inter (uppercase, +0.06em tracking) |
| Mono-data | 13 / 1.5 | 400 | JetBrains Mono |
| Mono-code | 12.5 / 1.55 | 400 | JetBrains Mono |

Minimum body size is 14px (desktop); never below 12px for meaningful labels.
Numeric columns (PC, instruction counts, durations, digests) use
`font-variant-numeric: tabular-nums`.

---

## 4. Spacing, radius, layout

### Spacing scale (4px base)

`4, 8, 12, 16, 24, 32, 48, 64` → tokens `--space-1` … `--space-8`.

### Radii

| Token | Value | Usage |
|---|---|---|
| `--radius-sm` | `6px` | Chips, inputs, buttons |
| `--radius-md` | `10px` | Cards, panels |
| `--radius-lg` | `14px` | Modals, sheets |
| `--radius-pill` | `999px` | Status pills, avatars |

### Elevation (dark-mode tuned: light from above, soft and low)

| Token | Value |
|---|---|
| `--shadow-sm` | `0 1px 2px rgba(0,0,0,0.4)` |
| `--shadow-md` | `0 6px 16px rgba(0,0,0,0.45)` |
| `--shadow-lg` | `0 18px 40px rgba(0,0,0,0.55)` |

Elevation is reinforced with `--border-subtle`, not with heavy shadows alone.

### Layout grid

- Window regions: **top command bar (56px)** · **left nav rail (240px, collapsible to 64px)** · **content** · **optional right inspector (360px)**.
- Content max width: 1600px for library grids; 1100px for documentation/forms.
- Gutters: 24px desktop, 16px compact (≤ 1024px).
- Breakpoints: **1024 / 1280 / 1440 / 1680**. The product targets ≥ 1280×720 minimum; layouts must remain usable at 1024×720 and 3840×2160.

---

## 5. Interaction states (mandatory for every interactive element)

| State | Treatment |
|---|---|
| Rest | token surface + `--border-subtle` |
| Hover | `--surface-3`; transitions 150ms `--ease-out`; never shifts layout bounds |
| Pressed | `--brand-subtle` fill, `transform: translateY(1px)` only |
| Focus-visible | 2px `--brand` outline, 2px offset; always visible; never removed |
| Selected | `--brand-subtle` fill + `--border-accent` left/outline |
| Disabled | 45% opacity, `cursor: not-allowed`, `aria-disabled`, no hover response |
| Loading | inline spinner or skeleton; button disabled during async |
| Error | `--status-danger` border + `role="alert"` message near the cause |

Cursor is `pointer` on every clickable element. Hover/press must not move
surrounding content (`transform`/`opacity` only).

---

## 6. Motion

- Durations: micro 150ms, standard 220ms, complex ≤ 320ms.
- Enter easing `cubic-bezier(0.16, 1, 0.3, 1)`; exit ~65% of enter duration.
- Animate `transform`/`opacity` only; never `width`/`height`/`top`/`left`.
- Motion expresses cause→effect (state change, navigation direction, progress),
  never decoration.
- Everything is wrapped so `@media (prefers-reduced-motion: reduce)` collapses
  transitions to ≤ 1ms and also disables named animations (skeleton shimmer,
  indeterminate progress fill), which are otherwise defined as `@keyframes`.

---

## 7. Component contracts

Every component below must ship Rest/Hover/Focus-visible/Pressed/Disabled, and
Loading/Error/Empty where the state is reachable.

- **App shell:** top command bar, left nav rail, content region, status bar.
- **Game card:** cover art (16:9 poster masked), title, region/serial (mono),
  **compatibility status pill**, primary action (Play when `playable`, otherwise
  the next valid action). Cover is a generated placeholder; never copyrighted art.
- **Status pill:** icon + label + colour; the canonical renderer for the
  lifecycle model (§8). Has a tooltip explaining the exact evidence requirement.
- **Build pipeline stepper:** one node per pipeline stage; each node is
  pending / running / succeeded / failed / skipped, with a real (non-fabricated)
  label. No percentage unless the backend emits progress.
- **Data table:** sortable (`aria-sort`), zebra-free (border-separated),
  sticky header, tabular numerics, row selection.
- **Inspector panel:** key/value diagnostics; mono values; copy affordance.
- **Console / log view:** mono, virtualised for long streams, severity filter,
  wraps long lines, selectable.
- **Dialog / modal:** 40–60% scrim, focus-trapped, Escape closes, primary action
  rightmost, destructive action visually separated in `--status-danger`.
- **Toast:** transient (3–5s), `aria-live="polite"`, never steals focus.
- **Empty state:** icon + one-line explanation + primary action (never a blank pane).
- **Skeleton / progress:** skeleton for unknown duration; determinate bar only
  when the backend reports a fraction.

---

## 8. Status vocabulary (canonical)

The lifecycle is defined in
`docs/product-blueprint/COMPATIBILITY_UX_MODEL.md`. Token mapping:

| Lifecycle state | Token | Icon family |
|---|---|---|
| Imported | `--status-neutral` | box |
| Analyzed | `--status-info` | scan |
| Translated | `--status-progress` | code |
| Native-built | `--status-built` | cpu |
| Boot-tested | `--status-test` | power |
| Playable | `--status-playable` | play |
| Import/Analysis/Build failed | `--status-danger` | alert |
| Translation incomplete | `--status-warn` | alert |
| Execution blocked / Unsupported | `--status-blocked` | lock |
| Recovery required | `--status-warn` | refresh |

---

## 9. Accessibility requirements (from retained generator baseline)

- Contrast: text ≥ 4.5:1 (normal), ≥ 3:1 (large/UI glyphs).
- **Measured (WCAG 2.1 relative-luminance, computed for this token set):**
  `text-primary` 16.0 / `text-secondary` 9.0 / `text-muted` 5.6 on `--bg-base`;
  `text-muted` 4.8 on `--surface-2`; status colours on `--bg-base` range
  4.8 (`--status-blocked`) to 9.3 (`--status-warn`); `--brand` on `--on-brand`
  6.2; `--run` on `--on-run` 8.2. **Lowest pair = 4.77:1.**
- **Caveat to re-verify at implementation:** a status pill paints its label on a
  same-hue 14% tint over a surface, which lightens that background and reduces
  headroom. The tightest cases (`--status-blocked` at 4.81 on base) must be
  re-measured **as composited**; if a chip falls below 4.5:1, lighten the status
  token or darken the chip tint rather than shipping it. Contrast is asserted
  from computation, not from visual inspection.
- Visible focus ring on all interactive elements; logical tab order.
- Full keyboard operation; controller maps to the same focus model.
- Icons are inline SVG (Lucide-style 1.5px stroke), never emoji.
- Meaningful images have text alternatives; decorative art is `aria-hidden`.
- Skip-to-content link in the shell.
- Errors announced (`role="alert"`/`aria-live`) and placed near the cause.
- `prefers-reduced-motion` respected.
- Every screen passes at 1024×720 with keyboard only.

## 10. Anti-patterns (do NOT use)

- Emoji as icons · raw hex in components · layout-shifting hover ·
  invisible focus states · pure-black backgrounds · neon/HDR/particle
  decoration · nested cards · sub-12px meaningful text · colour-only meaning ·
  fabricated progress percentages · glass effects that reduce readability.
