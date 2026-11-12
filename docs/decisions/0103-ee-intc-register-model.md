# ADR 0103: EE INTC register model (I_STAT / I_MASK)

- **Status:** Accepted
- **Date:** 2026-11-09
- **Scope:** shared C++20 runtime (`runtime/include/reps2/runtime/ee_intc.hpp`, `ee_state.hpp`), EE MMIO dispatch, HLE boot environment, generated-code CTest fixture
- **Depends on:** ADR 0073 (Timer 3 mode guard and MMIO dispatch precedent), ADR 0079 (guarded EE DMAC register contract), ADR 0091 (MMIO dispatch table and HLE DMAC boot state), ADR 0101 (EE semaphore services)

## Context

The M4 frontier is a `WaitSema` inside `sceSifBindRpc` that only an inbound IOP
RPC bind reply would release. Reaching and dispatching that reply needs the EE
interrupt path, and the EE interrupt controller (INTC) is its first stage. This
ADR models the two documented, CPU-visible INTC registers as a fail-closed
device, without delivering any CPU exception.

## Evidence

- **Observed (PCSX2 `pcsx2/Hw.h`):** `INTC_STAT = 0x1000F000`,
  `INTC_MASK = 0x1000F010`.
- **Observed (PCSX2 `pcsx2/HwWrite.cpp`):** a write to `INTC_STAT` performs
  `psHu32(INTC_STAT) &= ~value`, i.e. **write-one-to-clear**; a write to
  `INTC_MASK` performs `psHu32(INTC_MASK) ^= (u16)value`, i.e.
  **write-one-to-toggle over the low 16 bits** (bits 16 and above of the store
  are ignored).
- **Observed (PCSX2 `pcsx2/Hw.cpp`):** an interrupt source raises by
  `psHu32(INTC_STAT) |= 1 << n;` and the controller considers the line pending
  only when `(INTC_STAT & INTC_MASK) != 0`.
- **Observed (project-local PS2SDK `ee/include/kernel.h`, enum `INTC_*`):** the
  source numbers `INTC_GS` (0), `INTC_SBUS` (1), `INTC_VBLANK_S` (2),
  `INTC_VBLANK_E` (3), `INTC_VIF0` (4), `INTC_VIF1` (5), `INTC_VU0` (6),
  `INTC_VU1` (7), `INTC_IPU` (8), `INTC_TIM0` (9), `INTC_TIM1` (10),
  `INTC_TIM2` (11), `INTC_SFIFO` (13), `INTC_VU0WD` (14). `INTC_TIM3` (12) is
  reserved by the EE kernel for alarms.
- **Unknown / not claimed:** the hardware reset values of `I_STAT`/`I_MASK`; the
  routing of a particular source to a CP0 interrupt pin (IP2 vs IP3); and how a
  DMAC completion (for example SIF0) is presented to the INTC, if at all.

## Decision

1. Add `EeIntcState` holding `i_stat` and `i_mask`, each with a `*_known` flag.
   Every read, write, and source-raise fails closed (`false`) until the
   corresponding register is seeded; the MMIO handlers translate that to
   `Status::unsupported_memory_access`.
2. `write_stat` is write-one-to-clear; `write_mask` is write-one-to-toggle over
   the low 16 bits (`kEeIntcMaskToggleMask = 0x0000FFFF`), matching the observed
   PCSX2 semantics.
3. `raise_source(n)` sets one pending status bit and records pending state only;
   it delivers no exception. `line_asserted()` reports
   `(i_stat & i_mask) != 0` and returns `false` when either register is unknown,
   so an unseeded controller never reports an interrupt.
4. Register `0x1000F000` and `0x1000F010` in the `EeMemory` MMIO dispatch table.
5. `initialize_hle_boot_environment` seeds the idle INTC state (no pending
   source, all sources masked). This is an explicitly documented HLE model of the
   kernel-initialized state the replaced EE kernel presents, not a measured
   hardware reset value — the same standing as the ADR 0091 DMAC boot seed.
6. Add a generated-code CTest fixture (`reps2_intc_codegen_smoke`) whose guest
   program reads and writes both registers through the normal generated MMIO
   path.

## Alternatives considered

- **Deliver an EE interrupt exception now.** Rejected: it requires the CP0/IP2
  interrupt path, an exception vector entry, and a justified event source. The
  directive forbids building the generic interrupt subsystem first or calling a
  guest ISR without a modelled event path.
- **Wire the existing DMAC SIF0 completion (`publish_sif0_event`) into INTC.**
  Rejected: the SIF0→INTC routing is not established. The safe claim is D_STAT
  pending; mapping it to an INTC source would be invented.
- **Guess the reset values.** Rejected; the idle seed is labelled a model choice.

## Consequences

- The EE now exposes a correct, fail-closed I_STAT/I_MASK register file with the
  documented write semantics, which any later interrupt-delivery path consumes.
- No interrupt is delivered and no CP0 state changes; `line_asserted()` is the
  only signal exposed, and only when seeded.
- Unseeded production code still stops at the register access with
  `unsupported_memory_access`, exactly like the DMAC registers.

## Validation

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` (336 tests).
- CMake Release build and `ctest -C Release` — 34/34, including the new
  `reps2_intc_codegen_smoke`: unseeded fail-closed stop at the first register
  access; seeded guest W1C/W1T round-trip; distinct write semantics (only low 16
  bits toggle, zero write is a no-op); source raise and `line_asserted` gating;
  idle boot state readable and pending-but-masked deasserted.

## Limitations and next evidence

- No CPU exception delivery, no CP0 `Cause`/`Status` interaction, and no source
  timing.
- The DMAC/SIF0 → INTC (or DMAC interrupt line) routing is not modelled; wiring
  it is blocked on evidence.
- The idle reset values are an HLE model choice, not a measurement.
- This does not advance real-title execution; the M4 frontier is unchanged. It
  makes no boot or Playable claim.