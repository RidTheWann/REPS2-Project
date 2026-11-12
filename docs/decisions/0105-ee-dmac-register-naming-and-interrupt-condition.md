# ADR 0105: EE DMAC register naming and the interrupt-line condition

- **Status:** Accepted
- **Date:** 2026-11-09
- **Scope:** shared C++20 runtime (`runtime/include/reps2/runtime/ee_dmac.hpp`, `ee_state.hpp`), DMAC MMIO dispatch, DMAC smoke test
- **Depends on:** ADR 0091 (DMAC register file and MMIO dispatch), ADR 0103 (EE INTC register model), ADR 0104 (bind reply, whose probe reached the retry loop this ADR explains)

## Context

The SIF0 completion path could not be described correctly because two EE DMAC
register names were wrong in the runtime: `0x1000E000` was called `DPCR` and
`0x1000E020` was called `DICR` ("DMAC interrupt control register"). Neither name
matches the SDK or the reference behaviour, and the `DICR` comment claimed an
interrupt-control role that the real SIF0 completion does not use. A future
increment that wired SIF0 completion into the wrong interrupt path would have
been following this runtime's own words.

## Evidence

- **Observed (PS2SDK `ee/dma/include/dma_registers.h`):** `DMA_REG_CTRL` at
  `0x1000E000` (**D_CTRL**, `DMAE_DISABLE`/`DMAE_ENABLE`), `DMA_REG_STAT` at
  `0x1000E010` (**D_STAT**), `DMA_REG_PCR` at `0x1000E020` (**D_PCR**, "Priority
  Control Register", `DMA_SET_PCR(CPCOND, CDE, PCE)`).
- **Observed (PCSX2 `pcsx2/Hw.h`):** `DMAC_CTRL = 0x1000E000`,
  `DMAC_STAT = 0x1000E010`, `DMAC_PCR = 0x1000E020`; there is no `DICR` name.
- **Observed (PCSX2 `pcsx2/Dmac.h`):** `tDMAC_CTRL::DMAE` is bit 0;
  `tDMAC_PCR` holds `CPC` (0..9), `CDE` (16..25) and `PCE` (31);
  `tDMAC_STAT::TestForInterrupt()` returns
  `((_u16[0] & _u16[1]) != 0) || BEIS`.
- **Observed (PCSX2 `pcsx2/Hw.cpp`, `dmacInterrupt()`):** the line is suppressed
  when `D_CTRL.DMAE` is clear or when the hold register suspends the controller,
  and it is reported as `0x800` — COP0 `Cause` bit 11 (IP3) — while the INTC
  reports `0x400` (IP2).
- **Observed (PS2SDK `ee/kernel/src/sifcmd.c`):** the SIF0 receive path registers
  a per-channel handler with `AddDmacHandler(DMAC_SIF0)`, i.e. through the DMAC
  interrupt, not through `INTC_SBUS`.
- **Derived:** SIF0 completion is a DMAC interrupt on its own COP0 line; it is
  not an INTC source, and `0x1000E020` does not control interrupts.
- **Unknown:** the kernel's post-boot `D_CTRL` and hold-register values, so this
  increment assumes no reset value.

## Decision

1. Rename the runtime state and MMIO handlers to the SDK names:
   `ctrl`/`read_ctrl`/`write_ctrl` for `0x1000E000` (`kEeDmacCtrlAddress`) and
   `pcr`/`read_pcr`/`write_pcr` for `0x1000E020` (`kEeDmacPcrAddress`). Both keep
   their existing plain read/write, fail-closed-until-seeded behaviour; only the
   names and the documentation change.
2. Add `kEeDmacCtrlDmaeMask` (bit 0) and `kEeDmacDStatBusErrorStatusBit`
   (bit 15).
3. Add the pure function
   `ee_dmac_interrupt_pending(d_stat, dmae, controller_held)` implementing the
   documented condition: the low and high half-words of `D_STAT` share a set
   bit, or the bus-error status bit is set, and only while the master enable is
   set and the controller is not held. It takes its inputs from the caller, so
   no reset value is assumed and no device state is added.
4. Deliver no exception and change no CPU state. The function answers what the
   line would do for given register values; the COP0 IP3 exception path and the
   `AddDmacHandler` chain remain unimplemented.

## Alternatives considered

- **Keep the `DICR` name and document the correction only.** Rejected: the
  runtime header is the contract other increments read, and a wrong name plus a
  wrong comment is exactly what misleads the next change.
- **Model `D_CTRL` and the hold register as seeded device state now.** Deferred:
  their kernel-initialised values are unknown, and seeding a guess would create a
  false precondition for later interrupt work. The predicate takes them as
  explicit inputs instead.
- **Wire the predicate into exception delivery.** Rejected for now: there is no
  EE interrupt-exception path, and the `AddDmacHandler` chain records handlers
  without invoking them (ADR 0095). Delivering an interrupt would call guest
  code without a modelled event path.

## Consequences

- The runtime's DMAC names match PS2SDK and PCSX2, and the false
  "interrupt control register" claim is gone, so SIF0 completion cannot be
  mistaken for an INTC source.
- The documented DMAC interrupt condition is now a tested, side-effect-free
  building block that a later IP3 delivery path can consume without inventing
  reset values.
- Behaviour is unchanged: the probe trace is byte-for-byte identical to the
  previous revision because no address, seed, or guest-visible value changed.

## Validation

- `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked`.
- CMake Release build and `ctest -C Release` — 34/34, including
  `reps2_dmac_dstat_codegen_smoke`, which now also covers: a matched status/mask
  pair asserts; status alone or mask alone does not; the bus-error status bit
  asserts without a mask; the stall pair (SIS/SIM) uses the same comparison; a
  clear `DMAE` or a held controller suppresses the line; an unused upper bit or
  an idle register asserts nothing.
- Real-title probe: unchanged (renamed identifiers only).

## Limitations and next evidence

- No `D_CTRL`/hold-register device state, no IP3 exception delivery, no
  `AddDmacHandler` invocation, and no DMAC timing.
- `D_PCR` field semantics (channel priority, cycle stealing) are not
  interpreted; it is a plain read/write register.
- This does not advance real-title execution. The M4 blocker remains the IOP
  module behind the observed RPC bind sid (ADR 0104).