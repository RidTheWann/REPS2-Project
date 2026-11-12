# ADR 0115: `_DisableDmac` and the symmetric `D_STAT` interrupt-mask model

- **Status:** Accepted.
- **Date:** 2026-11-10.
- **Context:** With the SIFRPC server table in place (ADR 0114), the real-title probe completes the CdInit bind and call and then stops on `syscall` number `0x17` with `$a0 = 5` at `0x0025A454`, immediately after libcdvd creates its command semaphores. `_DisableDmac` is the next service the title requires, and the layer already models its sibling `_EnableDmac` (`0x16`).
- **Evidence:**
  - **Observed (PS2SDK `ee/kernel/include/syscallnr.h`):** `__NR__EnableDmac` is `0x16` and `__NR__DisableDmac` is `0x17`; the "i" variants are `-0x1c`/`-0x1d`, which the layer's magnitude dispatch already resolves.
  - **Observed (real-title probe, owner-supplied boot ELF; local evidence):** the post-CdInit path executes `0x0025A454` with `$a1 = 0x17` (service magnitude) and `$a0 = 5` (SIF0 channel) after four `CreateSema` calls. The previous stop was the CdInit bind retry loop.
  - **Observed (ADR 0096 and PS2Tek's EE register map, already recorded):** `D_STAT` bits 16..25 are the per-channel interrupt mask, "1 = enabled, write 1 to reverse", and the interrupt condition is `(status & mask) != 0`.
- **Decision:** Implement `_DisableDmac(channel)` as the exact mirror of `_EnableDmac`: reject a channel above 9 with the layer's documented negative failure value, read `D_STAT`, clear the channel's mask bit only when it is set (writing it when clear would toggle it on), leave an already-disabled channel untouched, and fail closed with `unsupported_memory_access` if the register state is unknown. On success `$v0` is left untouched because the kernel's return value is not established by target or public evidence.
- **Scope:** Disabling a channel only changes the interrupt mask. It does not stop a transfer, cancel a queued SIF0 event, or deliver an interrupt; REPS2 still delivers no DMAC interrupt. The observation fields record the call count, the last channel and the outcome (`disabled`, `already_disabled`, `invalid_channel`, `unknown_register_state`).
- **Alternatives considered:**
  1. Treat `_DisableDmac` as a no-op — rejected: it would leave the modelled mask disagreeing with a real kernel and hide the call.
  2. Write `D_ENABLER` instead of the `D_STAT` mask — rejected: the layer's established `_EnableDmac` model and the documented toggle semantics both address the `D_STAT` mask; introducing a second, unobserved register contract would contradict them.
  3. Return a success value in `$v0` — rejected: no evidence establishes it.
- **Consequences:** The EE DMAC mask model is now symmetric, and the title can continue past this kernel call. `D_CTRL`/hold behavior, the `AddDmacHandler` dispatch chain and EE interrupt delivery remain unimplemented.
- **Validation:** `reps2_hle_syscall_codegen_smoke` adds direct-call and generated-dispatch cases for clearing the SIF0 mask, preserving unrelated bits and status, the idempotent already-disabled path, the invalid-channel refusal, and `$v0` being untouched.
