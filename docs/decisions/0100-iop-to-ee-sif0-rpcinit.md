# ADR 0100: IOP-to-EE SIF0 RPCINIT handshake

- **Status:** Accepted
- **Date:** 2026-11-09
- **Scope:** shared C++20 runtime (`ee_state.hpp`, `ee_hle.hpp`, `ee_dmac.hpp`), SIF device state, EE-side SIFCMD receive dispatch, generated-probe observations
- **Related target:** Downhill Domination USA SCUS_971.77; observed guest PCs `0x0025C0D8` (`set_sreg`), `0x0025C1A0`..`0x0025C1C4` (`sceSifInitCmd`), `0x0025C108` (getter), `0x0025C928` (RPC wait loop)
- **Depends on:** ADR 0091 (DMAC boot state and MMIO dispatch), ADR 0092 (`sceSifSetDChain`), ADR 0097 (`sceSifGetReg` and the SIF register file), ADR 0098 (IOP SIFCMD command model), ADR 0099 (resident IOP SIFCMD)
- **Supersedes:** the ADR 0099/0098 statement that inbound IOP->EE SIF command delivery is not modelled.

## Context

ADR 0099 moved the real-title probe from the `SMFLAG` command-init poll to the
SDK SIF RPC wait loop at `0x0025C928`. That loop calls the getter `0x0025C108`,
which indexes a global 32-dword table at `0x00761C80`, and loops while entry 0 is
zero. The recorded M4 frontier was "what makes `word[0x00761C80]` non-zero".

This ADR answers that by reverse-engineering the exact producer and consumer of
`word[0x00761C80]` and modelling the minimum IOP->EE transaction that writes it.

## Evidence

- **Observed (pinned PS2SDK `d41ea62946e53d9a0c8788bb9eb8c8faf488b559`,
  `ee/kernel/src/sifrpc.c`, `sceSifInitRpc`):** registers the four RPC command
  handlers (`SIF_CMD_RPC_END/BIND/CALL/RDATA`, cids `0x80000008/09/0A/0C`); reads
  `sceSifGetReg(SIF_SYSREG_RPCINIT)` and returns if non-zero; otherwise builds a
  16-byte `SIF_CMD_INIT_CMD` packet with `cmdp[3] = 1` (opt `1`) and sends it;
  then blocks in `while (!sceSifGetSreg(SIF_SREG_RPCINIT));`, and finally runs
  `sceSifSetReg(SIF_SYSREG_RPCINIT, 1)`.
- **Observed (pinned PS2SDK `ee/kernel/src/sifcmd.c`):** `sceSifGetSreg(sreg)`
  returns `_sif_cmd_data.sregs[sreg]`; the system handler for `SIF_CMD_SET_SREG`
  (id 1) is `set_sreg`, which does `cmd_data->sregs[pkt->sreg] = pkt->val`; it is
  dispatched only from `_SifCmdIntHandler`, the handler registered for
  `DMAC_SIF0` (EE channel 5). The `cmd_data` structure is documented as binary
  compatible with the SCE libs and ps2lib, published in `SIF_SYSREG_MAINADDR`
  (`0x80000001`); `sregs` is the 8th pointer field (offset `0x1C`).
- **Observed (pinned PS2SDK `iop/system/sifrpc/src/sifrpc.c`, `sceSifInitRpc`):**
  after `sceSifInitCmd()` the IOP builds `SifCmdSRegData_t{ index = SIF_SREG_RPCINIT;
  value = 1 }` and `sceSifSendCmd(SIF_CMD_SET_SREG, ...)` to the EE receive buffer
  the EE published with `INIT_CMD`. The inbound `INIT_CMD` handler
  (`iop/system/sifcmd/src/sifcmd.c`) sets the `0x800` event flag when `opt != 0`
  and records `sci->m_sif_send_eebuf = pkt->m_newaddr` when `opt == 0`.
- **Observed (target disassembly, `mips64r5900el-ps2-elf-objdump`):**
  - `0x0025C108` (getter): `lui v0,0x76; sll a0,a0,2; addiu v0,v0,7296;
    addu a0,a0,v0; jr ra; lw v0,0(a0)` → `word[0x00761C80 + index*4]`.
  - `0x0025C204` (clear): `lui v0,0x76; li s0,31; addiu v0,v0,7296;
    addiu v0,v0,124` then a descending `sw zero` loop → zeroes 32 dwords
    `0x00761C80`..`0x00761CFC`.
  - `0x0025C0D8` (`set_sreg`): `lw v0,16(a0)` (index); `lw a2,28(a1)` (the
    `sregs` pointer at `cmd_data` offset `0x1C`); `lw v1,20(a0)` (value);
    `addu v0,v0,a2; sw v1,0(v0)` → `cmd_data->sregs[index] = value`. This
    confirms the `0x1C` offset directly in the retail image.
  - `0x0025C1C0` (in `sceSifInitCmd`): `addiu a0,a0,7296` (`a0 = 0x00761C80`);
    `sw a0,28(v0)` stores the `sregs` pointer into `cmd_data` at offset `0x1C`,
    where `v0 = 0x00761B58` (`&_sif_cmd_data`, the value the EE later publishes
    as `SIF_SYSREG_MAINADDR`).
  - Only one `jal` targets the getter: the wait loop at `0x0025C928`
    (`0x25C928: jal 0x25C108; 0x25C930: beqz v0, 0x25C928`).
- **Derived:** `word[0x00761C80]` is `_sif_cmd_data.sregs[0]`; it becomes 1 only
  through the `SIF_CMD_SET_SREG` system handler, which only runs from the SIF0
  (IOP->EE) DMAC receive dispatch. The wait therefore cannot be released without
  an inbound SIF0 command packet carrying `(index 0, value 1)`.
- **Observed (IOPRP270.IMG read-only from the target ISO, SHA-256
  `05c0b8c068903033dd85a383021f8d9a9630e685965b1497b2194c4c53110f63`):**
  the container holds 15 ELF32 `mips:3000` modules including resident `SIFCMD`
  (offset `0x29F0`, 10,377 bytes, SHA-256 `722e2d0d...`) and `SIFMAN`, plus the
  RPC servers `FILEIO`, `CDVDMAN`, `CDVDFSV`, `MODLOAD` — the modules that call
  the IOP-side `sceSifInitRpc`.
- **Unknown:** the exact retail IOP instruction that runs the IOP `sceSifInitRpc`
  (not located this increment; expected in an RPC server module or the IOP kernel
  ROM). Whether the retail SIFCMD/SIFMAN `.text` matches the pinned PS2SDK
  behaviour byte-for-byte. Whether any title needs the CPU-visible interrupt.

## Decision

1. Add the EE-side SIFCMD receive dispatch
   `process_ee_sif_command(EeState&, packet_addr)`: the model of the EE kernel's
   `_SifCmdIntHandler` and its system handlers. It reads a command packet from
   EE-visible memory and runs the handler for its id. Only `SIF_CMD_SET_SREG` is
   implemented; it writes `sregs[index] = value` where `sregs` is resolved from
   the guest-published `SIF_SYSREG_MAINADDR` plus `kEeSifCmdDataSregsOffset`
   (`0x1C`). It never writes a hardcoded address and never a hardcoded value.
2. Add `iop_sifcmd_rpc_init_reply(EeState&)`: the model of the resident IOP
   RPC-init reply. On the EE's `SIF_CMD_INIT_CMD` (`opt == 0`) handshake, it
   builds the documented `SIF_CMD_SET_SREG(index = SIF_SREG_RPCINIT, value = 1)`
   packet, writes the 24 bytes into the guest-published EE receive buffer
   (`sif.ee_pktbuf`), publishes the SIF0 completion event to `D_STAT` via the
   DMAC owner, and dispatches the packet through `process_ee_sif_command`. It runs
   at most once per boot and only when the resident IOP is modelled.
3. Add `EeDmacState::publish_sif0_event()`, which raises the channel-5 `D_STAT`
   status bit for an inbound transfer and fails closed when `D_STAT` is unknown.
4. Gate the reply on the resident device and a non-zero, writable EE receive
   buffer. If the buffer is not writable, `cmd_data` is unpublished, the `sregs`
   pointer is zero, or the index is out of range, nothing is written.
5. Report the new state in the generated probe (`sif_ee_inbound`,
   `sif_ee_inbound_cid`, `sif_ee_sreg0`, `sif_rpcinit_sent`, `sif_ee_sif0_event`)
   so a trace shows whether the handshake completed.

## Validation

- Rust: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`,
  `cargo test --workspace --locked` (335 main-suite tests plus the auxiliary
  suites), including `probe_bakes_budget_and_emits_a_machine_readable_trace`.
- CMake Release build and `ctest -C Release`, including
  `reps2_hle_syscall_codegen_smoke` and `reps2_native_build_driver_smoke`.
- New runtime cases in `hle_syscall_smoke.cpp`:
  - positive: with `cmd_data` published, the INIT_CMD handshake writes
    `sregs[0] == 1`, publishes the `D_STAT` SIF0 bit, records one inbound packet
    with cid `SIF_CMD_SET_SREG`, and marks the reply sent;
  - fail-closed: with `MAINADDR` unpublished, the transfer still happens but no
    register is written (`ee_sreg_base == 0`);
  - idempotent: a second INIT_CMD does not re-send the reply;
  - wrong cid: a non-`SET_SREG` inbound command writes nothing;
  - out of range: an index `>= 32` writes nothing.
- Real-title probe: regenerated on this revision from the extracted boot ELF with
  `--discover --tu-count 8 --native-build --parent-iso "<owned ISO>"
  --probe-budget 5000000 --hle-syscalls` and both relocated regions, built with
  MSVC Release, executed on the exact ISO (`0fe2b777...8a7eb86`) and boot ELF
  (`adfda7b7...f0cf0c`) digests. Observed trace after this ADR:
  `executed=3,169,504`, `status=14` (`unhandled_syscall`), `pc=0x0025A724`,
  `hle_number=0x44` (`$a0 = 3`), `syscalls=70`, `hle_handled=52`,
  `sif_ee_inbound=1`, `sif_ee_inbound_cid=0x80000001`, `sif_ee_sreg0=1`,
  `sif_rpcinit_sent=1`, `sif_ee_sif0_event=1`, `sif_cmd_pkts=3`,
  `sif_cmd_cid=0x80000009`, `sif_set_regs=3`, `sif_set_dma=3`. The title has left
  the `0x0025C928` wait loop, called `sceSifBindRpc` (outbound `SIF_CMD_RPC_BIND`),
  and now stops on `WaitSema` (0x44) waiting for the RPC bind reply. Compare the
  pre-ADR trace (ADR 0099): `executed=4,999,998`, `budget_exhausted`,
  `pc=0x0025C108`, `sif_cmd_pkts=2`, `sif_cmd_cid=0x80000002`.

## Consequences

- The EE RPC wait loop at `0x0025C928` is released: `sregs[0]` becomes 1 through
  the documented handler, so the guest proceeds past `sceSifInitRpc` and now
  blocks in `sceSifBindRpc` on `WaitSema` (0x44) at `0x0025A724`, waiting for the
  IOP's RPC bind reply. The new execution frontier is recorded in
  `docs/PROJECT_STATE.md`.
- The reply is delivered by a modelled SIF0 transfer with a `D_STAT` event. No
  IOP address, DMA completion, interrupt or kernel result is fabricated beyond
  the documented `(index 0, value 1)` reply, and the only guest-memory write is a
  `sregs[index] = value` through the guest's own published base.

## Limitations and next evidence

- No EE interrupt-exception path exists yet, so the EE-side system handler is
  replaced by HLE rather than run in interrupt context: REPS2 does not deliver a
  Level-1 interrupt from the `D_STAT`/INTC chain and does not run the guest ISR
  `_SifCmdIntHandler` at `0x0025C5F8`. Implementing that is the next fidelity
  step, not part of this ADR.
- The IOP-side `SCE` producer is modelled on the documented PS2SDK behaviour
  triggered by the EE's INIT_CMD handshake; the exact retail caller of the IOP
  `sceSifInitRpc` was not located in this increment.
- No guest-visible boot stage or Playable status is established by this ADR.
