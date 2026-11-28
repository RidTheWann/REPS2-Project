# ADR 0225: Q3 part 2 — GIF packet decoder + GS register/renderer + PNG dump

- **Status:** Accepted (autopilot) — owner may overrule
- **Date:** 2026-11-27
- **Scope:** `runtime/include/reps2/runtime/gs_renderer.hpp` (new),
  `runtime/include/reps2/runtime/ee_state.hpp`,
  `runtime/include/reps2/runtime/abi_version.hpp`,
  `CMakeLists.txt`, `tests/gif_decoder_smoke.cpp`,
  `tests/rg_reps2_engine_smoke.cpp`
- **Related:** ADR 0224 (DMAC ch2 chain-mode + GIF FIFO), ADR 0182 (GIF control regs),
  ADR 0175 (GS CSR), ADR 0219 (GS CRT display regs)

## Context

The Downhill Domination guest drives GS rendering through DMAC channel-2 chains
(ADR 0224) delivering 128-bit GIF qwords, plus direct guest SD stores to GS
MMIO registers (observed at `pc=0x21F044..0x21F094` writing PRIM/XYZ2/CLAMP/
MIPTBP/TEX). Q3 part 2 turns those qwords into a pixel framebuffer and dumps it
as PNG (TD-0 milestone). Formats are derived from the public reference
(ps2tek); title values only as a consistency check.

## Evidence

- **VERIFIED** (disassembly, capstone 5.0.7 MIPS64 LE on owner-supplied boot ELF
  `adfda7b7...f0cf0c`): DMAC ch2 chain-mode CHCR2=`0x145` writes at
  `0x210554/0x21F19C/0x224468/0x244158/0x244214` (DIR|MOD=chain|TTE|STR);
  GS MMIO `sd` stores at `0x21F044..0x21F094` writing PRIM (`0x12000000`),
  XYZ2 (`0x12000050`), CLAMP (`0x120000A0/E0`), MIPTBP (`0x12000080/0x12000090`),
  TEX (`0x12000060/0x12000070`).
- **VERIFIED** (two ABI-45 probes, `frontier.py compare` exit 0): 1,279,999,986
  instructions, 260 VBLANKs, `pc=0x25ABB0`; the guest reaches GS/DMA work each
  frame.
- **VERIFIED** (tests `gif_decoder_smoke.cpp`): PACKED A+D dispatch, PRIM tag
  field, IMAGE fail-closed, FRAME decode, SPRITE raster determinism,
  PSM/range fail-closed, PNG signature + byte-identity.

## Decision

1. **GIF packet decoder (`gif_consume_qword`, `gif_decode_tag`)** consumes each
   DMAC-delivered qword: a qword whose `in_payload` is false is a GIFtag parsed
   per ps2tek bit layout (NLOOP[0..14], EOP[15], PRIM-enable[46], PRIM data
   [47..57], FLG[58..59], NREGS[60..63]); subsequent qwords are payload
   dispatched as A+D writes into GS state per FLG:
   - PACKED (FLG=0): `NREGS` registers, each a 4-bit descriptor selecting a GS
     register; one 32-bit payload word per descriptor, little-endian order
     (bits 64-67 first).
   - REGLIST (FLG=1): same descriptor scheme.
   - IMAGE (FLG=2): not modelled; sets `gif_decode_unsupported` (fail closed).
   Unknown descriptor ids are ignored but counted; EOP/NLOOP==0 ends the packet.
2. **GS privileged-register MMIO** at the EE-side slots observed/expected from
   ps2tek Register_list.html: PRIM `0x12000000`, RGBAQ `0x12000010`, XYZ2
   `0x12000050`, XYOFFSET_1 `0x12000180`, SCISSOR_1 `0x12000400`, FRAME_1
   `0x120004C0`, ZBUF_1 `0x120004E0` (each modelled as 64-bit storage over two
   32-bit MMIO words). CSR/IMR/DISPFB2/DISPLAY2/BGCOLOR from ADR 0175/0219
   remain unchanged.
3. **Deterministic SPRITE rasterizer (`gs_draw_sprite_rgba32`)**: fills an
   RGBA32 rectangle in GS local memory at FRAME.FBP/FBW, only PSMCT32 (0); any
   other PSM or a rectangle leaving GS local memory is refused with no write
   (fail closed). Scissor/XYoffset parsing reserved as `ponytail:` (not applied
   yet).
4. **PNG dump (`png_write_rgba8`)**: minimal PNG (IHDR + one stored IDAT +
   IEND), Adler-32 over the uncompressed stream, deterministic bytes for
   identical pixels. Written only on request (host side), not per-VBLANK yet.
5. **GS framebuffer digest (`gs_framebuffer_digest`)**: FNV-1a over the FRAME
   region; deterministic across runs (BS-3 candidate).
6. **ABI 46 → 47** because `sizeof(EeState)` grows (gif_decoder + GS state +
   gs_local_memory). ABI-46 modules refused at admission.

## Tests (`tests/gif_decoder_smoke.cpp`)

- `test_gif_packed_ad_dispatch` — PACKED tag with NREGS=2 writes RGBAQ then XYZ2
  from the payload qwords (ps2tek vector).
- `test_gif_tag_prim_field` — tag PRIM-enable bit sets PRIM=SPRITE(6).
- `test_gif_image_format_flagged` — FLG=IMAGE sets `gif_decode_unsupported`.
- `test_gs_frame_field_decode` — FBP/FBW/PSM extraction.
- `test_gs_sprite_raster_deterministic` — SPRITE rectangle at FRAME base,
  byte-identical across two renders; digest stable.
- `test_gs_sprite_refuses_unsupported_psm` — non-PSMCT32 refused, memory
  untouched.
- `test_gs_sprite_refuses_out_of_range` — sprite past buffer boundary refused.
- `test_png_writer_deterministic` — PNG signature and byte-identity across two
  encodings.
- `rg_reps2_engine_smoke.cpp` — ABI-47 refuses ABI-46.

## Not modelled / NOT VERIFIED

- PNG dump triggered on VBLANK / frame boundary (host-side invocation only so
  far).
- BS-3 digest x2 cross-run equality (digest function exists; cross-run claim not
  exercised).
- Replay of recorded GS packet streams as differential tests (decoder exists;
  differential harness not written).
- Scissor/XYoffset application in the rasterizer (`ponytail:` ceiling).
- PNG digest vs framebuffer digest equivalence (two different hashes exist;
  not yet compared against a BS-3 reference).

## Gates

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` — pass (10 Rust suites).
- `cmake --build build --config Release --parallel` +
  `ctest --test-dir build -C Release` — 55/55 pass (Windows x64 / MSVC
  19.51).
