# ADR 0226: Q3 part 3 — GS framebuffer PNG dump per VBLANK + REGLIST fix

- **Status:** Accepted (autopilot) — owner may overrule
- **Date:** 2026-11-28
- **Scope:** `engine/src/main.cpp`, `runtime/include/reps2/runtime/ee_state.hpp`, `tests/gif_decoder_smoke.cpp`
- **Related:** ADR 0224 (DMAC ch2 chain-mode), ADR 0225 (GIF decoder + GS renderer)

## Context

TD-0 (PNG frame dump) requires the GS framebuffer to be written to a PNG file per
VBLANK so the host can inspect it. Q3 part 2 added the decoder/rasterizer/PNG writer
but no trigger. This slice wires the trigger and fixes a REGLIST decoder bug found
by independent QA of PR #64.

## Decision

1. **Per-VBLANK PNG dump (host-side only).** After each engine run batch
   (`run_engine` loop in `engine/src/main.cpp`), if `vblank_starts` advanced since
   the last dump and a `--png-dump <dir>` CLI option was given, write the current GS
   local memory framebuffer to `<dir>/frame_<n>.png` via `png_write_rgba8`.
   - Guest semantics untouched: the dump reads `gs_local_memory`/`gs_frame` and
     never writes guest-visible state.
   - Fail-closed: non-PSMCT32 frame, unconfigured frame, empty local memory, or a
     write failure stops cleanly; no fabricated pixels.
   - `gs_framebuffer_digest_last` is recorded at every VBLANK start in
     `materialize_ee_vblank` (pure computation, no I/O in the runtime path).
2. **REGLIST fix (QA finding on PR #64).** ps2tek GIF/Data_formats.html: in
   REGLIST mode every qword carries **two** registers (low 64 bits -> first
   descriptor, high 64 bits -> second). The PACKED path was incrementing
   `word_in_loop` twice (once in the branch, once after), which desynchronized
   the stream. Fixed: PACKED increments once (after the if/else), REGLIST
   increments by 2 in its branch.
3. **New test vector** `test_gif_reglist_two_registers_per_qword` proves two
   registers are written from a single qword (low -> RGBAQ, high -> XYZ2).
4. **Engine report** gains a `"gs"` object: `gif_qwords_delivered`, `tags_seen`,
   `ad_writes`, `sprites_drawn`, `unsupported_writes`, `framebuffer_digest_last`
   (hex). CLI `--png-dump <dir>` added to `Options`/`parse_options`/`print_help`.

## Evidence

- **VERIFIED** (ps2tek reference, quoted): REGLIST "Every quadword contains data
  for two registers." PACKED: one register per qword, low 64 bits.
- **VERIFIED** (tests): `gif_decoder_smoke.cpp` PACKED dispatch, REGLIST
  two-per-qword, PRIM tag field, IMAGE fail-closed, FRAME decode, SPRITE
  raster determinism, PSM/range fail-closed, PNG signature/identity.
- **INFERRED**: the title drives VBLANK ~59.94 Hz and renders each frame; the
  PNG dump fires per observed VBLANK start.
- **NOT VERIFIED**: cross-run BS-3 digest equality (digest function exists;
  cross-run claim not yet exercised end-to-end). Recorded-GS-stream replay
  differential not yet implemented.

## Gates

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` — pass (10 Rust suites).
- `cmake --build build --config Release --parallel` +
  `ctest --test-dir build -C Release` — 55/55 pass (Windows x64 / MSVC 19.51).