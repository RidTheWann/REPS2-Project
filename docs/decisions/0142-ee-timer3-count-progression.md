# ADR 0142: EE Timer 3 count progression on a deterministic guest-time source

- **Status:** Accepted (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** EE Timer 3 model (`ee_state.hpp`) plus the host step that drives it
  (`engine/include/rg_reps2_engine/session.hpp`). Runtime ABI 9 -> 10
  (`EeMemory` gains three timer fields).

## Context

`T3_COUNT` has been a 16-bit storage register since ADR 0134: guest writes set
it and reads return the stored value zero-extended, but nothing made it move.
ADR 0129 and ADR 0131 deliberately refused to return a synthetic counter, a host
clock value or an unstated cycle assumption, and left progression blocked on a
deterministic guest-time source.

Evidence for the hardware contract (PS2TEK EE Timers; PS2SDK
`ee/kernel/include/timer.h`):

- **Observed (documented register semantics):** `T3_MODE` bits are CLKS[1:0]
  (0 = BUSCLK, 1 = BUSCLK/16, 2 = BUSCLK/256, 3 = HBLANK), GATE, GATS, GATM,
  ZRET[6], CUE[7], CMPE[8], OVFE[9], EQUF[10], OVFF[11]. `EQUF` is set when the
  counter equals the compare value, `OVFF` when it wraps `0xFFFF`; both are
  cleared by writing one. The counter does not freeze on compare match; `ZRET`
  clears it at the match. Timer 3 has no `HOLD` register.
- **Derived (PS2TEK EE Timers):** HBLANK is one tick per scanline and an NTSC
  scanline is 858 pixels at 147.456 MHz / 13.5 MHz, i.e. about 9370 BUSCLK
  cycles; the model uses 9370.
- **Observed (title setup, `docs/RESEARCH.md`):** the title stores mode `0x183`
  (CLKS 3 HBLANK, CUE, CMPE) at `0x800763F4`/`0x80076470`, writes T3_COMP at
  `0x80076468` and reads T3_COUNT at `0x800761A4` with `timer3.mode=0x183`,
  `timer3.compare=60`.
- **Derived:** with CUE set and the HBLANK clock, the observed consumer needs a
  counter that advances deterministically; nothing in the available evidence
  fixes its absolute value, so the source must be modelled, not read from the
  host.

## Decision

1. Model a deterministic guest-time source: `EeMemory::guest_bus_cycles`, a
   monotonic count of modelled BUSCLK cycles advanced only by
   `advance_ee_timer3(EeState&, std::uint64_t)`. The host step
   (`EngineSession::run`) calls it with the instructions the batch executed, so
   the contract is **one guest instruction = one modelled BUSCLK cycle**. This
   ratio is an explicit, documented approximation chosen so the source is
   deterministic and host-clock independent; it is not a claim of cycle
   accuracy.
2. Materialise the counter on T3 register access
   (`materialize_ee_timer3`): while CUE is set, the counter is
   `timer3_base_count + elapsed_ticks`, where elapsed ticks are
   `(guest_bus_cycles - timer3_cycles_at_base)` divided by the mode's
   cycles-per-tick (1, 16, 256 or 9370). A compare crossing latches EQUF (and
   clears the counter when ZRET is set); a wrap past `0xFFFF` latches OVFF.
   `T3_MODE` writes are write-one-to-clear for EQUF/OVFF and keep the documented
   writable mask for the mode fields. `T3_COMP` and `T3_COUNT` writes re-base the
   elapsed window so ticks before the write are not lost.
3. No interrupt is delivered: `CMPE`/`OVFE` only affect the status bits. The
   INTC/exception path stays fail-closed (ADR 0103, S3).
4. The engine run report's `timer3` object gains `cycles`, the modelled
   BUSCLK count, so a probe can show the source advancing.
5. `EeMemory` gains `guest_bus_cycles`, `timer3_cycles_at_base` and
   `timer3_base_count`, so `REPS2_RUNTIME_ABI_VERSION` moves 9 -> 10.

## Consequences

- A guest that enables the counter and reads `T3_COUNT` now sees a deterministic,
  monotonic value derived from its own execution, not zero and not a host clock.
  Known granularity limit: the host advances the cycle source once per execution
  batch (`EngineSession::run`), and T3 register writes re-base the elapsed
  window from that last step, so a write in the middle of a batch sees the
  pre-batch cycle count and ticks elapsed since the batch start are then counted
  on top of it (over-counting bounded by one batch). Reads between steps see the
  value as of the last step. Both limits follow from driving the source at host
  steps; finer attribution would need a per-instruction runtime hook, which this
  slice deliberately does not add. **Superseded in part by ADR 0220**: the
  generated dispatcher now re-bases and materializes the cycle source at every
  block boundary (`sync_ee_timers`), so the batch is no longer the attribution
  unit. Per-instruction (sub-block) granularity is still not claimed.
- Timer 0/1/2 and Timer 3 interrupt delivery remain unmodelled.

## Validation

- `reps2_hle_syscall_codegen_smoke` gains
  `test_timer3_counter_progression_and_status_flags` (148 cases): CUE off freezes
  the counter; the HBLANK clock advances one tick per 9370 cycles; a compare
  crossing latches EQUF and writing one clears it without disturbing CUE; a wrap
  latches OVFF; ZRET clears the counter at the match.
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (Downhill Domination USA, boot ELF `adfda7b7...f0cf0c`,
  ISO `0fe2b777...a7eb86`, Windows x64/MSVC, runtime ABI 10,
  `PostADR140_142/run-report-abi10.json`): the run report's `timer3` object shows
  `cycles=79999980` — the modelled BUSCLK source advanced by the full executed
  instruction count — while `mode`/`compare`/`count` end at 0 because the title
  reconfigured the timer during the run. The observed early `T3_COUNT` read still
  returns a consistent stored value. Not a boot stage.
