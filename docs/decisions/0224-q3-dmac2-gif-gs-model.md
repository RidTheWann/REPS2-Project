# ADR 0224: Q3 — DMAC channel 2 chain-mode transfer + GIF input FIFO + GS register/local-memory model

- **Status:** Accepted (autopilot) — owner may overrule
- **Date:** 2026-11-27
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`, `runtime/include/reps2/runtime/ee_dmac.hpp`, `runtime/include/reps2/runtime/abi_version.hpp`, `engine/include/rg_reps2_engine/session.hpp`, `tests/dmac_dstat_codegen_smoke.cpp`, `tests/rg_reps2_engine_smoke.cpp`
- **Related:** ADR 0185 (DMAC ch2 bounded storage), ADR 0188 (ch2 STR completion), ADR 0182 (GIF control registers), ADR 0175 (GS CSR), ADR 0219 (GS CRT display registers)

## Context

The Downhill Domination title drives DMAC channel 2 (GIF) in chain mode each frame:
CHCR2 writes of `0x145` (chain-mode enable + STR) at `0x210554`, `0x21F19C`,
`0x224468`, `0x244158`, `0x244214`; TADR2 writes at `0x1B5728`, `0x210544`,
`0x2441B4`; MADR2 write at `0x210550`. GS MMIO `sd` stores (PRIM `0x12000000`,
XYZ2 `0x12000020`, CLAMP `0x120000A0/E0`, MIPTBP, TEX) at `0x21F044..0x21F094`
immediately precede the per-frame VBLANK wait at `0x25ABB0`. The rendering
pipeline goes through GIF DMA → GS → framebuffer, so Q3 (DMAC2 + GIF + GS
software renderer + PNG dump for TD-0) is justified.

## Decision

1. **Model DMAC ch2 chain-mode transfer** (`dma_chain_transfer_gif` in
   `ee_state.hpp`): on CHCR2 STR 0→1 with a chain-mode bit set, read the DMA tag
   quadword at TADR2, decode NLOOP/EOP/FLG/NREG/REGS/ADDR, copy NLOOP quadwords
   from MADR2 into the GIF input FIFO, follow ADDR to the next tag, stop at
   NLOOP==0 or EOP. PACKED (FLG=00) and REGLIST (FLG=01) are modelled; IMAGE
   (FLG=10) is fail-closed (`unsupported_memory_access`).
2. **Fail closed**: unreadable tag memory, full FIFO, or unsupported FLG returns
   `unsupported_memory_access` with no partial FIFO mutation. The channel still
   reaches its terminal state (STR self-clears) but the guest sees the error.
3. **New EeMemory state**: `gif_fifo[16]` (Gpr128 array + head/count),
   `gs_prim/gs_rgbaq/gs_xyz2/gs_xyz3/gs_tex0/gs_zbuf/gs_xyoffset/gs_scissor`,
   `gs_local_memory` (4 MiB vector, allocated in `EngineSession::start`).
4. **ABI bump 45 → 46** because `sizeof(EeState)` grows. ABI-45 modules are
   refused at admission as `runtime_abi_mismatch` (asserted in
   `rg_reps2_engine_smoke.cpp`).

## Evidence

- **VERIFIED** (disassembly, capstone 5.0.7 MIPS64 LE on owner-supplied boot ELF
  `adfda7b7...f0cf0c`): CHCR2=0x145 write sites, TADR2/MADR2/QWC2 address
  formations, GS MMIO `sd` stores at `0x21F044..0x21F094`.
- **VERIFIED** (probe `ap-abi45-mcprobe-02`/`-03`, ABI 45, `compare` exit 0):
  1,279,999,986 instructions, 260 VBLANKs, `pc=0x0025ABB0`; the title reaches
  GS/DMA work each frame.
- **VERIFIED** (tests): `dmac_dstat_codegen_smoke.cpp` chain push-to-FIFO
  (2 qwords from MADR2 with correct low/high) and fail-closed-on-unreadable-tag;
  `rg_reps2_engine_smoke.cpp` ABI-46 refuses ABI-45.

## Not modelled / NOT VERIFIED

- GIF tag A+D register dispatch into GS registers (next Q3 slice).
- GS software rasterizer and PNG frame dump (next Q3 slice, TD-0).
- VIF0/VIF1, VU1 microcode (Q4).
- Whether the title's chain mode uses the standard CHCR.MOD bits or bit 6 —
  the implementation accepts either (kEeDmacChcrModeMask covers bits 6,9,10).

## Tests

- `tests/dmac_dstat_codegen_smoke.cpp`: `test_chcr2_chain_transfer_pushes_gif_fifo`,
  `test_chcr2_chain_fails_closed_on_unreadable_tag`.
- `tests/rg_reps2_engine_smoke.cpp`: ABI-46 static_assert + ABI-45 refusal.

## Gates

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` — pass (10 Rust suites).
- `cmake --build build --config Release --parallel` + `ctest --test-dir build -C Release`
  — 54/54 pass (Windows x64 / MSVC 19.51).
