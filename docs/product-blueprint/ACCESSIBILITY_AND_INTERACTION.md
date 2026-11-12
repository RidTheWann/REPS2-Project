# REPS2 Accessibility & Interaction Model

Companion to `SCREEN_SPECIFICATIONS.md` and `design-system/reps2/MASTER.md` §5/§6/§9.
Covers the focus model, keyboard/controller maps, contrast, motion, screen-reader
semantics, density, responsive behaviour, and — explicitly — what is validated vs
not. Keyboard and controller drive **one** focus model; no controller-only paths.

---

## 1. Focus model

- **One focus ring, one order.** A single visible 2px `--brand` outline (2px
  offset, `MASTER.md` §5 `Focus-visible`). There is no second ring for the
  controller and no pointer-only affordance.
- **Logical order:** top-to-bottom, then left-to-right across shell regions:
  skip-to-content link → top command bar → left nav rail → content → right
  inspector → status bar. The order is independent of visual decoration.
- **Roving tab index** inside composite widgets (game grid, data tables, stepper,
  palette list): exactly one item is tabbable; arrows move within, `Tab` leaves.
- **Skip-to-content** link is the first tabbable element on every screen
  (`MASTER.md` §9).
- **Focus trap in modals:** S3 Add Game, S13 Command palette, S12 blocking
  dialogs, confirm dialogs. `Tab`/`Shift+Tab` cycle within; focus cannot leave.
- **Focus restore:** closing any overlay returns focus to the invoking element
  (e.g. `Esc` in S3 restores the Library `Add Game` button). Navigation via
  back (`B`/`Esc`) restores the prior screen's selected element.
- **Focus never hidden:** disabled controls use `aria-disabled` and stay
  focusable so their reason is announced; they are not removed from the order.

---

## 2. Keyboard map (full)

| Key | Action |
|---|---|
| `Tab` / `Shift+Tab` | Move focus forward/back within current scope |
| Arrow keys | Move within a composite (grid/table/list/stepper); open dropdowns |
| `Home` / `End` | First/last item in the focused composite |
| `PageUp` / `PageDown` | Page the focused list/console |
| `Enter` / `Space` | Activate the focused control (open game, run action) |
| `Esc` | Back one level; close overlay/dialog; stop a running session (S8, with confirm) |
| `LB`-equivalent: `Ctrl+PageUp` / `Ctrl+PageDown` | Switch top-level destination (rail) |
| `Y`-equivalent: `Ctrl+I` | Toggle the right inspector |
| `X` / `Ctrl+K` / `/` | Open command palette / search (S13) |
| `[` / `]` | Previous/next tab (S5 Game Detail) |
| `F5` | Refresh current data (e.g. S9 Developer Center) |
| `Ctrl+C` | Copy focused mono value (digest, PC, path) when a copy affordance is focused |

Shortcuts never override text-entry fields; `/`, `X`, `[` type literally while a
text input has focus.

---

## 3. Controller mapping

DualSense/Xbox-style map, on the **same** focus model:

| Input | Meaning |
|---|---|
| D-pad | Focus move (mirrors arrow keys) |
| Left stick | Focus move (analog, repeats like d-pad) |
| **A** (Cross) | Activate focused control (`Enter`) |
| **B** (Circle) | Back one level / close overlay (`Esc`) |
| **LB / RB** | Switch top-level destination (rail) |
| **Y** (Triangle) | Toggle the right inspector |
| **X** (Square) | Open command palette / search (S13) |
| Start/Options | Open command palette (alternate) |

No action exists only on controller or only on keyboard. Text entry (S3 file
picker, S11 forms) is delegated to the OS: file selection uses the native Windows
picker; when a virtual keyboard appears it owns focus per the same ring. Focus
order is identical across input devices.

---

## 4. Colour & contrast

Targets (`tokens.json.accessibility`, `MASTER.md` §2/§9):

- **Body text ≥ 4.5:1.** Measured for this token set: `--text-primary` 16.0,
  `--text-secondary` 9.0 (`--bg-base`); `--text-muted` 5.6 on `--bg-base` and
  4.8 on `--surface-2` — all pass; do not use `--text-muted` below 14px.
- **Large text / UI glyphs ≥ 3:1**; **focus ring ≥ 3:1** against adjacent surfaces.
- **Status chips ≥ 4.5:1 for the label.** Chip labels use the lighter
  `--status-*-text` tokens (`MASTER.md` §2.7) on a 14% alpha `--surface`
  background — never a full-fill block. Measured label contrast 4.78–7.40:1.
  Using the accent `--status-*` tokens directly on the tint would fail
  (3.61–4.05:1), which is why the text tokens are separate and required.
- **Colour is never the only signal.** Every status = icon (Lucide 1.5px, inline
  SVG) + text label + tooltip stating the exact evidence requirement
  (`MASTER.md` §2.6/§8). Numeric diagnostics that flag attention carry an icon +
  "above target" text, not colour alone. This covers `color-not-only`.
- **Disabled:** 45% opacity + `aria-disabled` + `cursor:not-allowed`; the disabled
  reason is a separate, full-contrast string (never conveyed by opacity alone).
- Icons are inline SVG, never emoji. Decorative cover placeholders are
  `aria-hidden`; meaningful covers carry the title as text alternative.

---

## 5. Motion & reduced motion

- Durations: micro 150ms, standard 220ms, complex ≤320ms; enter
  `cubic-bezier(0.16,1,0.3,1)`, exit ~65% (`MASTER.md` §6).
- Animate `transform`/`opacity` only; hover/press never shifts layout bounds.
- **`prefers-reduced-motion: reduce`** collapses all transitions to ≤1ms and also
  disables named `@keyframes` animations (skeleton shimmer, indeterminate
  progress fill), implemented by an explicit media query in the prototype
  stylesheet, not only by token duration overrides. Scroll/nav animations are
  disabled; progress indication falls back to static state text.
- Motion expresses cause→effect only (state change, nav direction, progress);
  it never carries meaning by itself.

---

## 6. Screen-reader semantics

- **Roles.** Shell: `banner` (top bar), `navigation` (rail), `main` (content),
  `complementary` (inspector), `contentinfo`/status role (status bar).
  Library: single-select `listbox` with `option` children and `aria-selected`
  (grid and list views). Tables: `table` with `row`/`columnheader` and
  `aria-sort`. Stepper: `list` of `listitem` with `aria-current="step"` on the
  active stage. Dialogs/palette: `dialog`/`aria-modal="true"` with an accessible
  name; focus is trapped and **restored to the invoker** on close.
- **Live regions.**
  - `aria-live="polite"`: toasts, palette result-count, import/build stage
    transitions, session counters.
  - `role="alert"` (assertive): errors at the cause (S3 validation, S4 stage
    failure, S12), which announce immediately.
- **Status pill:** exposes `aria-label="<state>: <one-line evidence summary>"`
  plus the tooltip text as the accessible description, e.g.
  *"Native-built: linked, ABI 3, cache key `<prefix>`"*.
- **Mono values** (PC, digests, register values) are announced as their literal
  text; group digests in readable chunks for speech.
- **Announce state, not decoration.** Focus into S4 announces the running stage
  name; focus into S8 announces the current stop reason when present.
- **Font size:** minimum body 14px; never below 12px for meaningful labels
  (`MASTER.md` §3). OS text scaling is honoured; layout reflows rather than clips.

---

## 7. Target size & density (desktop)

- `minTouchTargetPx = 44` (`tokens.json`). Desktop density: 32–36px control
  height with 44px hit area for primary/rail/controller targets; density does not
  shrink below the 44px interactive box.
- Row height ≥ 32px with 12px vertical rhythm; clickable rows are full-row targets.
- Console/log views are selectable, wrap long lines, and are virtualised.

---

## 8. Responsive behaviour

Windows desktop, minimum window `1024×720`; layouts must remain usable at
`1024×720` and `3840×2160` (`MASTER.md` §4, `tokens.json.layout`).

| Width | Rail | Inspector | Content | Notes |
|---|---|---|---|---|
| 1024×720 | 64px collapsed | hidden (toggle `Ctrl+I`/`Y`) | full-minus-rail | single-column forms; hud controls 44px; keyboard-complete |
| 1280 | 240px | hidden by default, toggle | standard grid | target min window |
| 1440 | 240px | 360px when relevant | grid + inspector | default desktop layout |
| 1680 | 240px | 360px | content max 1600px | library grid caps at 1600px |
| 3840×2160 | 240px | 360px | content max 1600px, centred | does **not** stretch; supports 200% OS scaling |

Gutters: 24px desktop, 16px compact (≤1024px). Content max width 1600px (library)
/ 1100px (forms and documentation). No horizontal scrolling of primary content at
1024px; tables drop optional columns before scrolling.

---

## 9. Interaction rules (ties to screen specs)

- Disabled actions stay visible with a reason (WCAG `empty-nav-state`;
  `COMPATIBILITY_UX_MODEL.md` §4). `Play` is disabled with its reason in every
  state except `Playable`, and replaced by "Run test session" in `Boot-tested`.
- Every overlay traps focus and restores it on close; `Esc`/`B` closes.
- Destructive confirms separate the destructive action visually
  (`--status-danger`) and place the primary action rightmost.
- Toasts are transient (3–5s), `aria-live="polite"`, never steal focus.
- Progress is honest: determinate only with a real fraction; otherwise
  indeterminate/skeleton (no fabricated percentages).

---

## 10. Validation status (explicit)

Three separate buckets — implemented-and-tested, source-derived, and unknown:

**Implemented in the prototype AND machine-tested** (headless-Chromium smoke
test, `product-prototype/tests/smoke.js`, 15 assertions): routing via hash,
library filter/search (including focus retention in the search field), grid/list
toggle, card selection, detail tabs, modal dialog open/`Esc`-close with focus
restore to the invoker, action gating (Play withheld for `Native-built`), toast
rendering, empty-state recovery, status-bar counts. Keyboard behaviour covered
by those assertions is limited to **`Esc` closing dialogs** and **input focus
retention** — nothing else.

**Source-derived (not runtime-tested in a shipped UI):**
- Contrast targets and token values from `tokens.json`/`MASTER.md` §2/§9;
  chip label contrast measured programmatically (4.78–7.40:1).
- Keyboard/controller parity and single-focus-model rule from
  `INFORMATION_ARCHITECTURE.md` §1 and `MASTER.md` §9.
- Motion/reduced-motion and target-size constants from tokens.

**NOT validated (unknown until the shell exists):**
- No REPS2 desktop shell is implemented on `feat/product-blueprint`; every
  keyboard shortcut other than the smoke-tested `Esc`/focus behaviour
  (`Tab`/`Shift+Tab` order, arrows, `Home`/`End`, roving tab index), every
  controller binding, focus order, live-region announcement, and breakpoint
  behaviour is **specified but unverified in a running product**.
- Physical controller hardware (DualSense/Xbox) is untested; no NVDA/Narrator
  pass has been run; WinUI 3 rendering and 200% OS scaling are unverified.
- Contrast for `--text-muted` at <14px is *flagged for verification* in
  `MASTER.md`; it has not been measured against the actual rendered chip.
- Real 1024×720 / 3840×2160 rendering is verified only via prototype
  screenshots (visual inspection), not on a shipped shell.
- No automated accessibility test (axe-like) exists in this repository.

Claims that a screen is accessible must be backed by a real build + assistive
testing pass; until then this document is a specification, not a certification.
