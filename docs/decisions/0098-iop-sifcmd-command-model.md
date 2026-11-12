# ADR 0098: IOP-side SIFCMD command model and the SIF register/DMA services

- **Status:** Accepted
- **Date:** 2026-11-08
- **Scope:** EE syscall HLE services `0x79` (`sceSifSetReg`), `0x77` (`sceSifSetDma`) and the bounded IOP-side SIFCMD command-packet model driven from those services; generated-code regression coverage
- **Related target:** Downhill Domination USA SCUS_971.77; observed guest PCs `0x0025C2D0`..`0x0025C33C`, SIF init block, stub `0x0025AAF0`
- **Depends on:** ADR 0092 (SIF0 channel start), ADR 0095 (`AddDmacHandler`), ADR 0096 (`EnableDmac`), ADR 0097 (`sceSifGetReg` and the SIF register file)
- **Correction:** Supersedes the ADR 0097 claim that the title polls `SMFLAG & 0x2`. The retail block loads `lui $s0, 0x2` and tests `$v0 & $s0`, i.e. `SMFLAG & 0x00020000`, which is PS2SDK `SIF_STAT_CMDINIT`. The title's convention therefore matches PS2SDK; the earlier "bit 0x2" statement was a misread.

## Context

ADR 0097 parked the real-title probe in the SDK's own `sceSifInitCmd` wait loop for the IOP `SMFLAG` command-init flag. The recorded M4 frontier was the IOP-side SIF command handshake — the resident IOP `SIFCMD` IRX and the flags it publishes. This ADR builds the bounded, evidence-led part of that frontier REPS2 can model without inventing an IOP, and the EE-side register/DMA services the SDK's `sceSifInitCmd` sequence calls.

## Evidence

- **Observed (PS2SDK pinned `d41ea62946e53d9a0c8788bb9eb8c8faf488b559`):** `common/include/sifcmd-common.h` defines a 16-byte `SifCmdHeader_t` (`psize:8`, `dsize:24`, `dest`, `cid`, `opt`) and the system command ids `SIF_CMD_CHANGE_SADDR` (`0x80000000|0`), `SIF_CMD_SET_SREG` (`|1`) and `SIF_CMD_INIT_CMD` (`|2`); `SET_SREG` carries `index` and `value` after the header. `ee/kernel/src/sifcmd.c` `_SifSendCmd` copies the packet from the EE source (`pkt`) into the IOP receive buffer (`_sif_cmd_data.iopbuf`), then `sceSifInitCmd` sends `CHANGE_SADDR` when the IOP sub-address is non-zero, otherwise waits for `SIF_REG_SMFLAG & SIF_STAT_CMDINIT` and sends `INIT_CMD` with `header.opt = 0`. `iop/system/sifcmd/src/sifcmd.c` registers the three system handlers: `_change_addr` assigns the payload to `m_sif_send_eebuf` (the IOP's **send-to-EE** destination); `_set_sreg` writes `m_sregs_ptr[index] = value`; the `INIT_CMD` handler sets event flag `0x100` and `sceSifSetMSFlag(SIF_STAT_CMDINIT)` when `opt == 0` (and event flag `0x800` and returns when `opt != 0`), then updates `m_sif_send_eebuf`. The IOP's own `sceSifInitCmd` sets `SMFLAG` to `SIF_STAT_CMDINIT` before waiting for event `0x100`.
- **Observed (target disassembly):** `0x0025C324` `lui $s0, 0x2` then `and $v0,$v0,$s0` polls `SMFLAG & 0x00020000`. The block preceding it reads `SIF_SYSREG_SUBADDR` (`0x0025C2D4`), branches on zero (`0x0025C2DC`), and on the zero path reads the hardware `SUBADDR`, writes the software `SUBADDR`/`MAINADDR` via `sceSifSetReg` (`0x79`), then sends a command. Disassembled with the project-local `mips64r5900-eabi` `objdump`.
- **Observed (probe, prior):** with only `sceSifGetReg` implemented, the probe executes the full 5,000,000-instruction budget and stops as `budget_exhausted` at `0x0025AAF8`, parked in that wait loop. Extracted ELF SHA-256 `adfda7b7...f0cf0c`.
- **Unknown:** whether the retail kernel's IOP build matches the pinned PS2SDK behaviour; the concrete IOP sub-address and packet-buffer values; the IOP event-flag mechanism; and whether any DMAC interrupt is required to drive command reception. None of these is established.

## Decision

1. Implement `sceSifSetReg` (`0x79`, `kEeSyscallSifSetReg`) sharing the ADR 0097 register file: it writes the addressed hardware (1..4) or software (`0x80000000`+0..2) register and records the write. An undocumented register number stops as `unhandled_syscall` with `unknown_register`, matching the read path.
2. Implement `sceSifSetDma` (`0x77`, `kEeSyscallSifSetDma`): validate `count` (1..2), read the `SifDmaTransfer_t` array from guest memory, program the SIF0 channel-5 registers (MADR5/QWC5/TADR5/CHCR5) through the DMAC owner exactly as `_SifSendCmd` does, start the channel with `CHCR_STR`, and return the 1-based transfer id. The DMAC owner still models a start as an immediately completed transfer (ADR 0091/0092); no interrupt is delivered.
3. Add a bounded IOP-side SIFCMD command model (`EeSifCmdState`, `process_sif_command`) that runs the three evidenced system handlers on a command packet the EE actually submits:
   - `CHANGE_SADDR` → `ee_pktbuf = payload` (the IOP's send-to-EE destination);
   - `SET_SREG` → `software_registers[index] = value` for a bounded index `< 32`, recording the last index/value;
   - `INIT_CMD` with `header.opt == 0` → `ee_pktbuf = payload`, `initialized = true`, `msflag |= SIF_STAT_CMDINIT`; with `opt != 0` the send buffer and flag are left untouched.
4. Parse the command packet from the EE **source** address of the `SIF_DMA_ERT` transfer entry, not the IOP destination: REPS2 models no IOP memory, so the still-resident EE copy is the only readable one.
5. **Do not seed `SMFLAG`/`MSFLAG`.** `initialize_hle_boot_environment` leaves the SIFCMD state uninitialised. Seeding `SIF_STAT_CMDINIT` would fabricate IOP readiness and unblock the title's wait loop without evidence; every SIF register still reads as zero, as in ADR 0097.
6. Keep the scope on SIFCMD. `SignalSema`/`WaitSema`/`DeleteSema` were prototyped while a (since-removed) fabricated DMA-completion signal was present; they are neither reached by the observed path nor part of this model, so they are not included.

## Validation

`tests/hle_syscall_smoke.cpp` gains SIFCMD cases and keeps the register/DMA cases:

- `INIT_CMD` with `opt == 0` sets `ee_pktbuf`, `initialized` and `MSFLAG` CMDINIT, and leaves `SMFLAG` zero; with `opt != 0` it changes none of them. The `opt` field is fixed at header offset 12 (the earlier test comment naming offset 20 was wrong).
- `CHANGE_SADDR` writes `ee_pktbuf` (IOP send-to-EE destination) and never `iop_pktbuf`.
- `SET_SREG` writes the bounded 32-entry software-register bank and records the last index/value; an out-of-range index is recorded but does not write the bank.
- `SifSetDma` with an `SIF_DMA_ERT` entry processes the packet read from the EE source address and publishes `MSFLAG`; the plain transfer case still does not signal any semaphore.
- The boot environment keeps the SIFCMD state uninitialised (`SMFLAG == 0`, `!initialized`).

Local Windows/MSVC verification and the full quality gates are recorded in `docs/PROJECT_STATE.md` for the merged revision.

## Consequences

- The EE-side services the SDK's `sceSifInitCmd` sequence calls (`0x79` `sceSifSetReg`, `0x77` `sceSifSetDma`) are modelled behind the existing fail-closed discipline, alongside the collected register file.
- The IOP-side SIFCMD command protocol is modelled from PS2SDK source and exercised by tests, but it is **not** reached by the current real-title trace: the title parks earlier, waiting for `SMFLAG` CMDINIT.
- No fabricated device state was introduced: no IOP-ready flag, no invented sub-address, no DMA data movement, no interrupt delivery.

## Limitations and next evidence

- The `SMFLAG` CMDINIT flag is written by the IOP over SIF; REPS2 has no IOP, so the title's wait loop cannot legitimately complete. Finishing the handshake requires an M5 IOP subsystem, not a seeded flag.
- The IOP SIFCMD model runs the resident handlers inline in the EE's `SceSifSetDma`; it does not model IOP memory, event flags, or the SIF1 interrupt that would actually drive dispatch.
- The SIF0 channel model still treats a start as immediately complete and delivers no interrupt, so the completion handler the title registers is never invoked.
- No guest-visible boot stage or Playable status is established by this ADR.
