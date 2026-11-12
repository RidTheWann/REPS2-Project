# ADR 0091: MMIO register dispatch table and the HLE DMAC boot state

- **Status:** Accepted
- **Date:** 2026-11-08
- **Scope:** C++20 EE runtime MMIO routing, DMAC register file (DPCR/D_STAT/D_PCR, SIF0 CHCR/MADR/QWC/TADR), HLE boot-environment initialization, generated-probe evidence field
- **Related target:** Downhill Domination USA SCUS_971.77; observed EE addresses `0x1000E010`, `0x1000C000`
- **Supersedes scope of:** ADR 0079 (extends its D_STAT contract to a small DMAC register file and an evidenced boot state)

## Context

ADR 0079 gave the EE DMAC `D_STAT` register a bounded, fail-closed read/write contract but left the guest unable to read it during a real-title run: `EeDmacState` has no default state, so `read_d_stat` returns `unsupported_memory_access` and the probe stopped at guest PC `0x0025C274` with 3,168,708 executed instructions. The observed path then reads SIF0 `CHCR` at `0x1000C000` and tests `CHCR_STR` `0x100`.

The guest code that reaches this boundary (`sceSifInitCmd` pattern) is the SDK's SIF command initialisation. The real EE kernel runs before the title and leaves the DMAC in a defined idle state; REPS2 replaces that kernel with an HLE layer, so it must reproduce the state the title depends on. Refusing every DMAC read forever is not a correct HLE boot environment; inventing a pending SIF0 event would be worse.

Only one 32-bit MMIO register was routed before this change (D_STAT), plus the Timer 3 mode word, each as a hard-coded `if` in `load_u32`/`store_u32`. Adding SIF0 channel registers one address at a time in the same way would not scale and would scatter device knowledge through the memory helper.

## Evidence

- **Observed (target):** the generated block at `0x0025C264` builds `0x1000E010`, executes `lw`, masks bit `0x20`, conditionally writes back, then builds `0x1000C000` and tests bit `0x100`. Recorded in `docs/RESEARCH.md` and reproduced locally from the extracted ELF.
- **Observed (PS2SDK registers):** `common/include/ee_regs.h` defines `A_EE_D_CTRL 0x1000e000`, `A_EE_D_STAT 0x1000e010`, `A_EE_D_PCR 0x1000e020`, and the SIF0 channel (5) registers `A_EE_D5_CHCR 0x1000c000`, `A_EE_D5_MADR 0x1000C010`, `A_EE_D5_QWC 0x1000C020`.
- **Observed (PS2SDK idle semantics):** PS2SDK's own `dma_reset` (R5900 code, `ee/lib/libdma.erl`, symbol at `.text` offset `0x0`) iterates the ten channels and clears each channel's control/status words; the SDK-visible idle DMAC is therefore "all channels stopped, no pending status". Disassembled with the local `mips64r5900el-ps2-elf-objdump`.
- **Observed (PS2SDK SIF0 status):** `common/include/sifcmd-common.h` defines the SIF0 status flag the guest masks.
- **Observed (next boundary):** with the D_STAT/SIF0 path no longer refusing, the probe advances 9 instructions and stops at `0x0025AAC4` as `unhandled_syscall` for service `$v1 = 0x78`. The guest wrapper at `0x0025AAC0` is `li $v1, 0x78; syscall; jr $ra` (disassembled from the ELF), and PS2SDK's `libkernel.erl` names `0x78` (`sceSifSetDChain`) with sibling `0x77` (`sceSifSetDma`). This is recorded as the next requirement, not implemented here.
- **Non-normative cross-check:** PCSX2's legacy `dmacReset()` sets `stat = 0` and every `chcr = 0`. It is an implementation cross-check, not a hardware specification.

## Decision

1. Add a small MMIO dispatch table to `EeMemory` (`runtime/include/reps2/runtime/ee_state.hpp`). Each entry is one register address with a read and a write handler taking `(const) EeMemory&`; `load_u32`/`store_u32` consult the table first and fall through to main RAM otherwise. Helper signatures and generated-code emission are unchanged.
2. Extend the DMAC owner (`ee_dmac.hpp`) from D_STAT to DPCR (`0x1000E000`), D_STAT (`0x1000E010`), D_PCR (`0x1000E020`), and SIF0 (channel 5) CHCR/MADR/QWC/TADR. Each register keeps the same "known" discipline: reads and writes fail closed until the state is established.
3. Add `EeDmacState::seed_idle_boot_state()` and `reps2::runtime::initialize_hle_boot_environment(EeState&)`. The latter is the HLE boot environment the replaced kernel would present: every modelled DMAC register is known and idle. It is applied **explicitly** by a host that opts into the HLE layer; without it, every DMAC access still fails closed. This is a modelling decision about the kernel-initialized state, not a claim about a hardware reset value.
4. Model SIF0 start as an immediately completed transfer: a `CHCR5` write that raises `STR` (0→1) publishes the SIF0 completion event to `D_STAT` bit 5 and self-clears `STR`, matching the "channel finished" observation the SDK polls for. It does nothing when `D_STAT` is unknown, and never fabricates an event for a channel that was already running. Other `CHCR` bits are preserved.
5. Extend the generated probe trace with a `dmac_boot=%u` field so a run records whether the HLE boot DMAC state was applied.
6. Keep the boundary honest: `SifSetDChain` (0x78) and `SifSetDma` (0x77) are **not** implemented by this ADR. The probe reports them as `unhandled_syscall` rather than faking a SIF DMA start.

## Validation

- `tests/dmac_dstat_codegen_smoke.cpp` now covers: unknown-state fail-closed for every new register; seeded read/write for DPCR/D_PCR/CHCR5/MADR5/QWC5/TADR5; the `CHCR5` `STR` start → `D_STAT` SIF0 event → W1C clear sequence; and `initialize_hle_boot_environment` producing readable zeroed registers followed by the same start/clear sequence.
- Local Windows/MSVC Release build: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked` (all pass), `cmake --build build --config Release`, and `ctest -C Release` (33/33 pass, including `reps2_dmac_dstat_codegen_smoke` and `reps2_native_build_driver_smoke`).
- Real-title probe: regenerated from the extracted ELF (`SHA-256 adfda7b7…f0cf0c`) with both relocated regions and `--discover --hle-syscalls`, the probe reports `dmac_boot=1`, executes 3,168,717 instructions, and stops at `0x0025AAC4` as `unhandled_syscall` `0x78`.

## Consequences

- The D_STAT/SIF0 device-state boundary that blocked M4 is cleared, and the next requirement is identified precisely (`sceSifSetDChain`/`sceSifSetDma`).
- Production semantics stay fail-closed: a runtime that does not opt into the HLE boot environment cannot read a DMAC register.

## Limitations and next evidence

- The SIF0 transfer model has no IOP backpressure, timing, or real channel state; it assumes immediate completion. This is a documented simplification, not observed hardware behavior.
- `D_PCR` (`0x1000E020`) and `D_CTRL` (`0x1000E000`) now use the PS2SDK register names; earlier revisions of this ADR called them `DICR` and `DPCR`, and the claim that PCSX2 calls `0x1000E020` `DICR` was wrong — PCSX2 names it `DMAC_PCR` too. The correction, the bit evidence and the documented DMAC interrupt condition are in ADR 0105.
- Only SIF0 (channel 5) is modelled. Other DMAC channels, DPCR/D_PCR field semantics, SIF DMA chain traversal, SIF1, and IOP interaction remain unimplemented.
- Next: implement the SIF DMA syscalls the observed path needs (`0x77` `sceSifSetDma`, `0x78` `sceSifSetDChain`) behind the same fail-closed discipline, then re-run the probe.
- No guest-visible boot stage or Playable status is established by this ADR.
