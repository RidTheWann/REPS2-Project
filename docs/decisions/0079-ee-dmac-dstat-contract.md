# ADR 0079: Guarded EE DMAC D_STAT Register Contract

- **Status:** Accepted
- **Date:** 2026-11-07
- **Scope:** C++20 EE runtime MMIO routing, DMAC state ownership, D_STAT read/write semantics, generated-code regression coverage
- **Related target:** Downhill Domination USA SCUS_971.77; observed EE address `0x1000E010`
- **Extended by:** ADR 0091, which grows this owner into a small DMAC register file behind an `EeMemory` MMIO dispatch table and adds an explicit HLE boot state; the D_STAT contract here is unchanged.

## Context

The latest bounded native probe reaches guest PC `0x0025C274` after 3,168,708 guest instructions and stops on a word read from `0x1000E010`. PS2SDK names this address `DMAC_COMM_STAT` and defines the SIF0 status flag as `0x20`. The target masks the read word with `0x20` and conditionally writes the bit back. This proves the address and the guest's test/clear pattern; it does not prove the initial D_STAT value, the existence of a pending SIF0 event, or the complete SIF/IOP behavior.

The register map distinguishes channel status bits from interrupt-mask fields. Treating the register as ordinary RAM, returning zero on read, or treating every write as a normal assignment would create false guest behavior.

## Evidence

- [PS2Tek EE register map](https://israpps.github.io/ps2tek/) identifies D_STAT at `0x1000E010`, channel status flags in bits 0..9 with write-one-to-clear behavior, interrupt masks in bits 16..25 with write-one-to-toggle behavior, and additional stall/MFIFO mask fields at bits 29..30. It also identifies the D_STAT channel status/mask model.
- [PS2SDK `ee/kernel/src/sifcmd.c`](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/sifcmd.c) names `0x1000e010` as `DMAC_COMM_STAT`, `0x20` as `STAT_SIF0`, and in `sceSifInitCmd` reads the register, writes the SIF0 bit back when present, then checks SIF0 CHCR `0x1000c000` / `CHCR_STR` `0x100`.
- [PCSX2 legacy `Dmac.h`](https://sources.debian.org/src/pcsx2/1.6.0%2Bdfsg-2/pcsx2/Dmac.h) independently corroborates the register field layout, including SIS/MEIS/BEIS status fields and SIM/MEIM mask fields. It is an implementation cross-check, not a normative hardware specification.

## Decision

1. Give the DMAC register state a dedicated owner, `runtime/include/reps2/runtime/ee_dmac.hpp`, rather than continuing to grow the generic EE memory header.
2. Represent whether the D_STAT value is known separately from its numeric value. A default `EeDmacState` is unknown; zero-initialized host memory is not asserted to be a hardware reset value.
3. For a known state:
   - Reads expose only the documented register fields; reserved bits read as zero.
   - Writes of one to channel status bits 0..9 clear those bits (W1C).
   - Writes of one to mask bits 16..25 and 29..30 toggle those mask bits; zero preserves them.
   - Writes that set bits 13..15 are refused as `unsupported_memory_access` because this slice has not established their write/acknowledgement behavior.
4. An unknown D_STAT read or write remains `unsupported_memory_access`. Refusals do not change the output value, known flag, or register state.
5. Do not invent DMA completion events, SIF0 pending state, IOP responses, reset values, interrupts, or CHCR transitions. These belong to future device/event components driven by separately evidenced behavior.
6. Route guest `load_u32` / `store_u32` through the DMAC-owned register contract. Leave all unmodeled addresses fail-closed.

## Validation

A generated C++20 smoke fixture compiles the actual guest instruction sequence:

```text
lui  $v0, 0x1000
ori  $v0, $v0, 0xE010
lw   $v1, 0($v0)
andi $v1, $v1, 0x20
sw   $v1, 0($v0)
```

The tests cover:
- unknown-state read and write failures, including preservation of the destination sentinel and guest state at the fault boundary;
- known-state SIF0 bit-5 acknowledgement while unrelated status and mask fields are preserved;
- write-one-to-clear channel status versus write-one-to-toggle interrupt masks;
- refusal of unmodelled bits 13..15 without partial state mutation.

The source-only change has not yet been built on the user's Windows/MSVC environment. Required CMake/CTest and Rust checks must pass in CI or a local Windows run before merge.

## Limitations and next evidence

This ADR implements the register's bounded access contract, not an operational DMAC/SIF implementation. The current Downhill probe cannot consume a real SIF0 status unless a device/test supplies a known D_STAT state. The next runtime slice must identify the producer of the SIF0 status bit, its initial condition, channel-5 CHCR transitions, and IOP/SIF interaction from the target path and independent references. Do not seed a title-specific constant in production merely to advance this trace.

No guest-visible boot stage or Playable status is established by this ADR.
