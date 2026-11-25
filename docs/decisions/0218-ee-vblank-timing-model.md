# ADR 0218: EE VBLANK synthetic timing model (INTC sources 2/3)

- **Status:** Accepted (validated locally; real-title probe pending at time of acceptance).
- **Date:** 2026-11-25
- **Scope:** `runtime/include/reps2/runtime/ee_state.hpp`
  (`kEeVblankCyclesPerFrame`/`kEeVblankDurationCycles` constants, `EeMemory`
  VBLANK state fields, `materialize_ee_vblank`, called from `advance_ee_timers`),
  `engine/src/main.cpp` (run-report `vblank` fields), `tests/vblank_timing_smoke.cpp`
  (7 synthetic cases), `docs/RESEARCH.md`, `docs/PROJECT_STATE.md`.
- **ABI:** `REPS2_RUNTIME_ABI_VERSION` 41 → 42; `sizeof(EeState)` grows; ABI-41 modules
  refused at admission.
- **Related:** ADR 0103 (INTC register model), ADR 0142/0172 (Timer 2/3 progression),
  ADR 0175/0178 (no GS interrupt/vsync fabrication), ADR 0217 (Downhill VBLANK
  frontier gate opened by this model).

## Context

The Downhill Domination (SCUS_971.77) frontier probe ended at `pc=0x0025ABB0`:

```
0x0025ABB0: lui   $3, 0x1000          ; base of EE INTC registers
0x0025ABB4: lw    $2, 0($3)           ; $2 = I_STAT (0x1000F000)
0x0025ABB8: and   $2, $2, 4           ; test bit 2 (INTC_VBLANK_S)
0x0025ABBC: nop
0x0025ABC0: nop
0x0025ABC4: beq   $2, $0, 0x0025ABB0  ; loop while bit 2 is clear
0x0025ABC8: nop
```

This is a genuine hardware wait: the guest polls `I_STAT` bit 2 until VBLANK_START is
asserted. REPS2 raised no INTC source 2 (only Timer 2 raises source 11), so the loop
spun until the instruction budget, ending `budget_limit_reached` at `pc=0x0025ABB0`
after 79,999,926 instructions over 16 batches (`syscalls_seen=1,636`,
`syscalls_handled=1,619`, callbacks `82/82`, `dmac.interrupts_delivered=0`,
`intc.stat=0`, `timer2.irq_raises=0`).

Nothing in ADR 0175/0178 forbids *modeling* this event; they forbid fabricating GS
state/interrupts and fabricating a one-shot condition bit just to let a trace pass.
Modeling the actual periodic VBLANK source deterministically from guest time is
consistent with Timer 2 (ADR 0172).

## Decision

1. **Runtime model** (`runtime/include/reps2/runtime/ee_state.hpp`):
   - `kEeVblankCyclesPerFrame = 4'920'954` cycles (~59.94 Hz at 294.912 MHz EE bus)
     and `kEeVblankDurationCycles = 487'443` cycles (~26/262 lines of blank).
     These are **synthetic timing constants**: one modeled guest BUSCLK cycle per
     executed instruction (ADR 0142). The division is non-integral; the model is
     named synthetic and is **not** hardware-accurate, and it models no GPU,
     display, or field parity.
   - `EeMemory` gains `vblank_next_start_cycle`, `vblank_next_end_cycle`,
     `vblank_starts`, `vblank_ends`, `vblank_irq_refusals`, `vblank_in_blank`.
   - `materialize_ee_vblank(EeMemory&)` raises INTC source 2 at
     `frame*frame_cycles - kEeVblankDurationCycles` and source 3 at each
     `(frame+1)*frame_cycles` boundary, through `EeIntcState::raise_source` — the
     same path as Timer 2 (ADR 0172). Large jumps between engine steps process
     every missed transition in order; repeated pending bits coalesce naturally
     (INTC state remains pending regardless of `I_MASK`). `I_STAT` writes remain
     write-one-to-clear.
   - Called from `advance_ee_timers(EeState&, bus_cycles)`, driven by
     `state.memory.guest_bus_cycles` (one cycle per executed instruction).
   - No GPU state, display output, vsync, GS DMA, field parity, or GS CSR
     VSINT/FIELD is modeled or fabricated.

2. **Codegen dispatcher** (`engine/include/rg_reps2_engine/session.hpp`): already
   calls `advance_ee_timers` after each execution boundary; no call-site change
   needed.

3. **Engine report** (`engine/src/main.cpp`): adds `vblank` object
   `{starts, ends, irq_refusals, in_blank}`.

4. **Tests** (`tests/vblank_timing_smoke.cpp`): 7 synthetic cases — bit 2 not set
   before period, set at period and bit 3 at boundary, multiple raises over a
   large jump, I_STAT W1C clears only bit 2, polling visible through `I_MASK`,
   deterministic replay across independent states, fail-closed when INTC is
   unseeded.

5. **ABI**: `REPS2_RUNTIME_ABI_VERSION` 41 → 42 (`sizeof(EeState)` grows). Generated
   modules must be rebuilt; ABI-41 modules refused.

## Consequences

- Downhill Domination frontier at `pc=0x0025ABB0` is unblocked: VBLANK_START now
  fires deterministically from guest time, so the `lw/and/beq` wait loop exits when
  `I_STAT` bit 2 is set, instead of spinning on the instruction budget.
- Probes record the `vblank` report object; validity is judged against real-title
  `run-report.json` (outcome, final PC, instruction count, `intc.stat`, `vblank`
  counts), not against boot/playable claims.
- `I_MASK` semantics unchanged: polling `I_STAT` works even while masked; only
  delivery (IP2 interrupt entry) is gated.
- Remaining unknowns out of scope: exact GS VSINT/FIELD timing, odd/even field
  parity, PAL vs NTSC frame-rate selection, console-specific frame timing, and any
  guest behavior after this wait loop exits. Those require a new ADR + evidence.

## Validation

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D
  warnings`, `cargo test --workspace --locked` (Rust gates pass; no Rust changes
  required by this slice).
- CMake Release build + `ctest --test-dir build -C Release --output-on-failure`
  passes (including new `reps2_vblank_timing_smoke`, 7 cases).
- Real-title probe `frontier-vblank-01` on merged `main` c79b460, runtime ABI 42,
  Windows x64/MSVC, ISO `0fe2b777…a7eb86`, boot ELF
  `adfda7b7…f0cf0c`. Recorded in `docs/RESEARCH.md`; no boot-tested or playable
  claim is made.

## References

- Sony EE Core Instruction Set Manual v6.0 / EE Core User's Manual: INTC source
  numbering (`INTC_VBLANK_S` = 2), `INTC_STAT = 0x1000F000` write-one-to-clear,
  `INTC_MASK = 0x1000F010`.
- PS2SDK `ee/include/kernel.h`: `INTC_*` source enum.
- PCSX2 `pcsx2/Hw.h`: `INTC_STAT = 0x1000F000` (behavioral cross-check only).
- ADR 0103: EE INTC register model.
- ADR 0142/0172: Timer 2/3 progression and `raise_source` path reused here.
- ADR 0175/0178: no GS interrupt/vsync fabrication — this model stays strictly
  within that boundary.
- ADR 0217: Downhill VBLANK frontier gate opened by this model.