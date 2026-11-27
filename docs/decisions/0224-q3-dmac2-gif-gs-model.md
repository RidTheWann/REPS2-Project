# ADR 0224: Q3 — DMAC channel 2 source-chain transfer + GIF delivery + GS register/local-memory model

- **Status:** Accepted (autopilot) — owner may overrule
- **Date:** 2026-11-27
- **Scope:** `runtime/include/reps2/runtime/ee_dmac.hpp`, `runtime/include/reps2/runtime/ee_state.hpp`, `runtime/include/reps2/runtime/abi_version.hpp`, `engine/include/rg_reps2_engine/session.hpp`, `tests/dmac_dstat_codegen_smoke.cpp`, `tests/rg_reps2_engine_smoke.cpp`
- **Related:** ADR 0185 (DMAC ch2 bounded storage), ADR 0188 (ch2 STR completion), ADR 0182 (GIF control registers), ADR 0175 (GS CSR), ADR 0219 (GS CRT display registers)

## Evidence policy

This ADR derives the hardware formats from the **public reference** and uses the title's observed
values only as a consistency check (MASTER_PROMPT §5).

- **Reference (cited):** ps2tek `PS2/DMAC/Chain_mode.html` and `PS2/DMAC/IO.html`
  (https://israpps.github.io/ps2tek/). The DMAtag and CHCR layouts below are quoted from it.
- **Observed (consistency check):** the Downhill Domination boot ELF
  (`adfda7b7...f0cf0c`) writes CHCR2=`0x145` at `0x210554`, `0x21F19C`, `0x224468`,
  `0x244158`, `0x244214`, with TADR2 writes at `0x1B5728`, `0x210544`, `0x2441B4` and a MADR2
  write at `0x210550`. `0x145` decomposes as DIR(1) | MOD=chain(4) | TTE(0x40) | STR(0x100),
  which is consistent with a source chain that also transfers the tag data word.
- **Derived:** the ID-rule tables, the TTE ordering, and the register update rules.
- **Not verified:** whether the title exercises every tag ID (REFE/CNT/NEXT/REF/REFS/CALL/RET);
  only the mechanics are modelled, exercised by synthetic per-ID vectors.

## Hardware formats (from the reference)

### DMAtag, 128 bits
| Bits | Field |
|---|---|
| 0..15 | QWC (quadwords to transfer) |
| 16..25 | unused |
| 26..27 | PCE (priority control) |
| 28..30 | ID (chain tag ID) |
| 31 | IRQ |
| 32..62 | ADDR (lower 4 bits zero) |
| 63 | SPR (ADDR memory select) |
| 64..127 | data word, transferred only when CHCR.TTE=1 |

### CHCR (D_CHCR)
| Bits | Field |
|---|---|
| 0 | DIR (direction) |
| 2..3 | MOD (0=normal, 1=chain, 2=interleave) |
| 4..5 | ASP (address stack pointer) |
| 6 | TTE (transfer DMAtag) |
| 7 | TIE |
| 8 | STR |
| 16..31 | TAG (bits 16..31 of the last DMAtag) |

### Source-chain ID rules (verbatim from the reference)
- `0 REFE`: `MADR=DMAtag.ADDR; TADR+=16; tag_end=true`
- `1 CNT`: `MADR=TADR+16; TADR=MADR`
- `2 NEXT`: `MADR=TADR+16; TADR=DMAtag.ADDR`
- `3 REF`: `MADR=DMAtag.ADDR; TADR+=16`
- `4 REFS`: `MADR=DMAtag.ADDR; TADR+=16`
- `5 CALL`: `MADR=TADR+16; ASR[ASP]=MADR+(QWC*16); TADR=ADDR; ASP++`
- `6 RET`: `MADR=TADR+16; TADR=ASR[--ASP]` (or `tag_end` when the stack is empty)
- `7 END`: `MADR=TADR+16; tag_end=true`

Also from the reference: *"MADR and TADR update while a transfer is ongoing"*, and
*"When Dn_CHCR.TTE is on, bits 64-127 are transferred BEFORE QWC."*

## Decision

1. **Decode the DMAtag with its real field layout** (`kEeDmacTag*` constants): QWC, PCE, ID, IRQ,
   ADDR, SPR, data word. The earlier draft wrongly reused GIF-tag fields (NLOOP/EOP/FLG/NREG);
   that is removed.
2. **Decode CHCR from the real layout** (`kEeDmacChcrModMask` = bits 2..3, `kEeDmacChcrTteMask`
   = bit 6). The ad-hoc `0x640` chain mask is removed. Normal (MOD=0) stays bounded storage;
   chain (MOD=1) performs the transfer; interleave (MOD=2) fails closed with an explicit status.
3. **Implement every source-chain ID rule exactly** as quoted, including CALL/RET address-stack
   behaviour and the REFE/END termination.
4. **Transfer order:** when TTE=1 the tag data word (bits 64..127) is delivered before the QWC
   payload, matching the reference.
5. **Atomic commit with a stated ceiling.** The chain is staged into a bounded buffer
   (`kEeGifStagingLimitQwords` = 65536) and delivered only if the whole chain succeeds. Any
   unreadable tag, unreadable payload, or over-ceiling chain returns
   `unsupported_memory_access` with **no** delivered qwords and **no** MADR/TADR/QWC change.
   A test proves each case (`test_chcr2_chain_fails_closed_on_unreadable_tag`,
   `test_chcr2_chain_fails_closed_mid_payload`).
   **Deliberate simplification (`ponytail:`):** the ceiling plus staging exists because the GIF
   packet consumer (Q3 part 2) is not yet wired; upgrade path is streaming delivery to the decoder,
   which removes the ceiling.
6. **Deliver into a bounded consumer interface**, not a fixed FIFO: `gif_qwords_delivered`
   counts every delivered qword and `gif_head_capture[8]` keeps the first eight for diagnostics.
   A 16-qword buffer is gone, so a real chain cannot stall the title on a full FIFO.
7. **New EeMemory state** also carries GS privileged-register storage (PRIM, RGBAQ, XYZ2/XYZ3,
   TEX0, ZBUF, XYOFFSET, SCISSOR) and a 4 MiB `gs_local_memory` vector for Q3 part 2.
8. **ABI 45 → 46** because `sizeof(EeState)` grows; ABI-45 modules are refused at admission.

## Tests (vectors derived from the reference layout)

- `test_chcr2_chain_transfer_pushes_gif_payload` — REF (QWC=2) then END (QWC=0): payload delivered
  in order, MADR/TADR/QWC follow the ID rules.
- `test_chcr2_chain_fails_closed_on_unreadable_tag` — tag at the last 8 bytes of RAM: error, zero
  delivered, MADR/TADR/QWC unchanged.
- `test_chcr2_chain_fails_closed_mid_payload` — first tag OK, second tag's payload unreadable:
  error, zero delivered, registers unchanged (atomic).
- `test_chcr2_chain_tte_delivers_tag_data_first` — TTE=1: the tag data word is delivered (QWC=0
  for that tag), proving the ordering rule.
- `rg_reps2_engine_smoke.cpp` — ABI-46 refuses an ABI-45 module.

## Not modelled / NOT VERIFIED

- GIF *packet* (the 128-bit tag inside the data stream) parsing and A+D register dispatch — Q3 part 2.
- GS software rasterizer, framebuffer readback, PNG dump, BS-3 digests — Q3 part 2 (TD-0).
- Interleave mode (MOD=2): explicit fail-closed.
- Scratchpad-sourced ADDR (tag bit 63 SPR=1): parsed but not used as a source.
- Which tag IDs the title actually uses: inferred from the chain-mode usage, not observed per-ID.

## Gates

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` — pass (10 Rust suites).
- `cmake --build build --config Release --parallel` + `ctest --test-dir build -C Release`
  — 54/54 pass (Windows x64 / MSVC 19.51).