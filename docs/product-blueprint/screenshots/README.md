# Prototype screenshots

Headless Chrome captures (1440×900) of `product-prototype/`, regenerated with:

```
chrome --headless=new --disable-gpu --hide-scrollbars \
  --window-size=1440,900 --virtual-time-budget=5000 \
  --screenshot=<out>.png "file:///<repo>/product-prototype/index.html#<route>"
```

| File | Route | Screen (IA id) |
|---|---|---|
| `01-library.png` | `#library` | S1 Library (grid) |
| `02-library-empty.png` | `#library?filter=none` | S2 Empty / no-match |
| `03-add-game.png` | `#addgame?d=add` | S3 Add Game dialog |
| `04-pipeline.png` | `#pipeline?id=g-downhill` | S4 Import & Build workflow |
| `05-game-detail.png` | `#detail?id=g-downhill&tab=overview` | S5 Game Detail |
| `06-compatibility.png` | `#detail?id=g-downhill&tab=compatibility` | S6 Compatibility lifecycle |
| `07-build-queue.png` | `#queue` | S7 Build Queue |
| `08-developer.png` | `#developer` | S9 Developer Center |
| `09-developer-caps.png` | `#developer?caps=true` | S9 `dev capabilities` |
| `10-execution-report.png` | `#report?id=g-downhill` | S10 Execution report |
| `11-settings.png` | `#settings` | S11 Settings |
| `12-error-dialog.png` | `#library?d=error` | S12 Error & Recovery (dialog) |
| `13-relink-needed.png` | `#detail?id=g-sce&tab=compatibility` | S12 Recovery / relink state |
| `14-library-list.png` | `#library?view=list` | S1 Library (list) |
| `15-library-1024x720.png` | `#library` | S1 Library at minimum window size (1024×720) |

Content was verified by asserting expected DOM markers per route (13 routes
asserted, all passed) because this environment cannot display images inline.
15 captures exist: the 13 asserted routes, plus `09-developer-caps` and
`15-library-1024x720` (same route, different fragment/size). All data is
simulated; see `../../product-prototype/README.md`.
