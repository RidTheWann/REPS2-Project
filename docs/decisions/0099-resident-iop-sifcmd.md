# ADR 0099: Resident IOP SIFCMD device and SIF register ownership

- **Status:** Accepted
- **Date:** 2026-11-09
- **Scope:** shared C++20 runtime (`ee_state.hpp`, `ee_hle.hpp`), SIF device state, IOP SIFCMD residency, generated-probe observations
- **Related target:** Downhill Domination USA SCUS_971.77; observed guest PCs `0x0025C2B0`..`0x0025C33C`, stubs `0x0025AAE4`/`0x0025AAF4`/`0x0025AAA4`
- **Depends on:** ADR 0091 (DMAC boot state), ADR 0097 (`sceSifGetReg` and the SIF register file), ADR 0098 (IOP SIFCMD command model)
- **Supersedes:** the ADR 0097/0098 statement that the boot environment must leave every SIF flag at zero.

## Context

ADR 0097 parked the real-title probe in the SDK's own `sceSifInitCmd` wait loop
for the IOP `SMFLAG` command-init flag. ADR 0098 modelled the three IOP-side
`SIFCMD` system commands but deliberately left `SMFLAG` unset, so the title could
not proceed. The recorded M4 frontier was "the resident IOP `SIFCMD` that
publishes `SMFLAG` CMDINIT".

This ADR resolves the frontier by modelling that resident module as device
state. The distinction from "seeding a flag" turns on *who owns the bit*: the
new state is produced by a device startup transaction, is idempotent, and is
reported by the probe as device state (`sif_iop`), rather than being written
unconditionally into a register array by the boot environment.

## Evidence

- **Observed (pinned PS2SDK `d41ea62946e53d9a0c8788bb9eb8c8faf488b559`,
  `iop/system/sifcmd/src/sifcmd.c`):** the IOP module's `_start` calls
  `sceSifInit()` then `sceSifInitCmd()`. `sceSifInitCmd` executes
  `sceSifSetSMFlag(SIF_STAT_CMDINIT)` and then blocks on event flag `0x100`.
  `_start` finishes with `sceSifSetSubAddr((u32)g_sif_cmd_data_table.m_pckt_buffer)`.
  The inbound `INIT_CMD` handler runs
  `if (pkt->m_header.opt) return; sceSifSetMSFlag(SIF_STAT_CMDINIT); sci->m_sif_send_eebuf = pkt->m_newaddr;`.
- **Observed (same pin, `ee/kernel/src/sifcmd.c`):** the EE `sceSifInitCmd` reads
  `SIF_SYSREG_SUBADDR`. When it is non-zero the EE sends `SIF_CMD_CHANGE_SADDR`.
  When it is zero the EE runs
  `while (!(sceSifGetReg(SIF_REG_SMFLAG) & SIF_STAT_CMDINIT));`, reads
  `SIF_REG_SUBADDR`, stores it in `SIF_SYSREG_SUBADDR`, stores
  `&_sif_cmd_data` in `SIF_SYSREG_MAINADDR`, and sends `SIF_CMD_INIT_CMD` with
  `opt = 0`.
- **Derived (register ownership):** `SMFLAG` is written by the IOP
  (`sceSifSetSMFlag`), `MSFLAG` is written by the EE (`sceSifSetMSFlag`),
  `SADDR` is written by the IOP (`sceSifSetSubAddr`), and the `SIF_SYSREG_*`
  software registers are written by the EE kernel. The register file is
  therefore not a free-form EE scratch array.
- **Observed (target disassembly, `mips64r5900el-ps2-elf-objdump`):**
  `0x0025C2D4` calls `sceSifGetReg(0x80000000)`; `0x0025C2DC` `beqz $v0,0x0025C324`;
  `0x0025C328`..`0x0025C334` poll `sceSifGetReg(4) & 0x00020000`;
  `0x0025C33C` reads hardware register 2. `0x0025C928` calls `0x0025C108`,
  which indexes a global table at `0x00761C80`; `0x0025C930` loops while the
  entry is zero.
- **Observed (probe before this ADR):** budget-exhausted after
  `5,000,000` guest instructions at PC `0x0025AAF8`, `203,526` syscalls,
  `hle_name=SifGetReg`, `$a0 = 4` — parked in the `SMFLAG` poll loop.
- **Observed (probe after this ADR):** budget-exhausted after `4,999,998` guest
  instructions at PC `0x0025C108`, `65` syscalls (`48` handled), `$v1 = 0x77`
  (`SifSetDma`), `sif_iop=resident`, `sif_smflag=0x00020000`,
  `sif_msflag=0x00020000`, `sif_get_regs=4`, `sif_set_regs=2`,
  `sif_set_dma=2`, `sif_cmd_pkts=2`, `sif_cmd_cid=0x80000002`,
  `sif_cmd_init=1`. The `SMFLAG` loop is gone; the title completes
  `sceSifInitCmd`, adopts the (zero) sub-address, publishes
  `SYSREG_SUBADDR`/`MAINADDR`, sends `INIT_CMD`, and enters the SIF RPC wait
  loop at `0x0025C928`.
- **Inferred:** on real hardware the pre-existing `SIF_SYSREG_SUBADDR` is non-zero
  because the EE kernel has already initialised SIF against the IOP, so the
  retail title takes the `CHANGE_SADDR` branch and does not poll at all. REPS2's
  HLE *is* the kernel, and the kernel loads the IOP replacement image, so the
  modelled machine has a resident `sifcmd`; its startup is what publishes
  CMDINIT here.
- **Unknown:** the IOP command-packet buffer address; whether the retail
  `IOPRP270.IMG` build matches the pinned PS2SDK behaviour; the SIF1 interrupt
  path and the response the RPC wait loop is polling for.

## Decision

1. Model the IOP `SIFCMD` module's residency as device state:
   `EeSifCmdState::residency` (`absent`/`resident`) and `startup_count`.
2. Add `iop_sifcmd_start(EeState&)`, which performs the documented startup
   publication exactly once: the device becomes resident and `SMFLAG` gains
   `SIF_STAT_CMDINIT` in the shared register file. It is idempotent.
3. `initialize_hle_boot_environment` runs that transaction. The replaced EE
   kernel loads the IOP replacement image, so a booted machine has a resident
   `sifcmd`; this is the same reasoning as ADR 0091's kernel-initialised DMAC
   idle state.
4. Do **not** publish an IOP `SADDR` value. `sifcmd` also calls
   `sceSifSetSubAddr`, but that address lives in IOP RAM, which REPS2 does not
   model; inventing one would hand the title a buffer address it programs into
   SIF DMA structures. `SADDR` therefore stays unmodelled and reads as zero.
5. Never write `MSFLAG` from the device. It is EE-written and is set only by an
   inbound `INIT_CMD` with `opt == 0`, as in ADR 0098.
6. Move the SIF register-number and status-flag constants next to the device
   state in `ee_state.hpp`, so the register file and the constants that define
   it live together.
7. Report the device in the generated probe (`sif_iop`, `sif_smflag`,
   `sif_msflag`, `sif_hw_subaddr`, `sif_get_regs`, `sif_set_regs`,
   `sif_set_dma`, `sif_cmd_pkts`, `sif_cmd_cid`, `sif_cmd_pktsize`,
   `sif_cmd_init`) so a trace shows the handshake without a per-packet log.

## Validation

- Rust: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` (335 main-suite tests plus the auxiliary suites).
- CMake Release build and `ctest -C Release` (33/33), including
  `reps2_hle_syscall_codegen_smoke` and `reps2_native_build_driver_smoke`.
- New runtime cases: the boot environment starts the device (`sif_iop=resident`,
  `SMFLAG & CMDINIT`, `MSFLAG == 0`, `SADDR == 0`); the startup is idempotent
  and an absent device still reports zero; the observed poll predicate
  `sceSifGetReg(4) & 0x00020000` is satisfied only when the device is resident.
- Real-title probe: the before/after traces quoted above, on the exact ISO
  (`0fe2b777…8a7eb86`) and boot ELF (`adfda7b7…f0cf0c`) digests, `--discover`,
  both relocated regions, `--parent-iso`, `--hle-syscalls`, budget `5,000,000`.

## Consequences

- The M4 frontier moves from the `SMFLAG` wait loop to the SIF RPC wait loop at
  `0x0025C928`, which polls a global table at `0x00761C80` for a non-zero entry.
  That is the next evidenced requirement: IOP-side SIF RPC service plus the
  SIF1 interrupt and EE-side SIF DMA completion delivery.
- No address, DMA completion, interrupt or kernel result was fabricated. The only
  new device-visible value is the documented CMDINIT bit, published by a device
  startup transaction that is reported in the trace.

## Limitations and next evidence

- `SADDR` reads as zero while the device reports CMDINIT. That is an explicit
  partial model: REPS2 has no IOP RAM, so it cannot back a real buffer address.
  The zero value is inert today because the SIF DMA model never dereferences
  `dest`, but it is not a faithful IOP address.
- `sifcmd` also waits on event flag `0x100` for the EE's `INIT_CMD`; REPS2 models
  the handler effect (the `MSFLAG` publication and the EE buffer record) but not
  the event-flag mechanism or IOP thread resumption.
- SIF1 interrupt dispatch, EE INTC delivery, and SIF DMA completion interrupts
  remain unimplemented, which is why the RPC wait loop cannot be observed to
  complete.
- No guest-visible boot stage or Playable status is established by this ADR.