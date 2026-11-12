# ADR 0117: `sceSifStopDma` and the SIF0 start-bit stop model

- **Status:** Accepted.
- **Date:** 2026-11-10.
- **Context:** After `RemoveDmacHandler` (ADR 0116) the real-title probe stops on `syscall` `0x6B` at `0x0025A9B4`, called from the retail `SifIopReset` at `0x25EBE4`. PS2SDK maps `__NR_sceSifStopDma` to `0x6B` and describes the entry as "Disables SIF0 (IOP -> EE)". The layer already models the SIF0 start path (`sceSifSetDChain`, `sceSifSetDma`, ADR 0092/0098) but had no stop path.
- **Evidence:**
  - **Observed (PS2SDK `ee/kernel/include/kernel.h:482`):** `extern void sceSifStopDma(void); // Disables SIF0 (IOP -> EE).`
  - **Observed (PS2SDK `ee/kernel/src/iopcontrol.c:42`):** `SifIopReset` calls `sceSifStopDma()` before building and sending the reset command packet.
  - **Observed (boot ELF, local evidence):** the wrapper at `0x25A9B0` loads `$v1 = 0x6B` and traps; the only caller in the image is `SifIopReset` at `0x25EBE4`, immediately before it reads `SIF_SYSREG_SUBADDR` and fills the reset packet.
  - **Observed (real-title probe):** the stop is `hle_number=0x6B` with `$a0` pointing at the caller's stack frame (the entry takes no argument); the preceding services are `DisableDmac(5)` and `RemoveDmacHandler(5, 5)`.
  - **Behavioural cross-check (PCSX2 `pcsx2/Sif0.cpp`):** the SIF0 completion interrupt clears `CHCR.STR` (`sif0ch.chcr.STR = false`), so the start bit is the channel's run state.
- **Decision:** Model the stop as clearing `CHCR5.STR` and preserving the other `CHCR` fields. REPS2 models every SIF0 start as an immediately completed transfer that self-clears `STR` (ADR 0086), so an idle channel is unchanged; the service never claims to have cancelled a transfer it did not model. An unknown `CHCR5` state stops with `unsupported_memory_access` rather than writing an invented register value. PS2SDK declares the entry `void`, so `$v0` is not written. The call count and whether a start bit was actually cleared are recorded.
- **Scope and non-claims:** `CHCR` chain traversal, in-flight SIF0 cancellation, `D_CTRL`/hold state, the IOP reset command itself and any IOP reboot remain unimplemented in this ADR's implementation. ADR 0118 subsequently adds an explicitly modelled resident-HLE reset completion, but does not execute the requested IOP image or claim its services are available.
- **Alternatives considered:**
  1. Treat the stop as a no-op and not implement the service — rejected: the guest's call would stop the run, and a documented, low-risk register operation is available.
  2. Write zero to the whole `CHCR5` — rejected: the documented stop concerns the start bit, and clearing direction/size fields has no evidence.
  3. Cancel the last modelled SIF0 completion event — rejected: events are published on start and are not queued; there is nothing to retract.
- **Consequences:** The observed `SifIopReset` prologue can complete. The IOP reset packet and module loading behind it are the next architectural question and are not claimed by this decision.
- **Validation:** `reps2_hle_syscall_codegen_smoke` adds a case that seeds `CHCR5` with `STR` plus an unrelated bit, checks only `STR` is cleared, checks the already-stopped and unknown-state paths and that `$v0` is untouched, plus a generated-code dispatch of `0x6B`. The regenerated real-title probe reports `sif_stop_dma=1 sif_stop_dma_cleared=0` (the channel was already idle), processes the following `SIF_CMD_RESET_CMD` packet (`sif_cmd_cid=0x80000003`) as an unmodelled IOP command, and then exhausts its 5,000,000-instruction budget in the retail post-reset `SifIopSync` loop at `0x0025ED10`, which reads `SMFLAG` and waits for `SIF_STAT_BOOTEND` (`0x40000`). The modelled IOP does not reboot, which is the recorded next frontier.
