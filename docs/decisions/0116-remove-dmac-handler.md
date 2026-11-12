# ADR 0116: `RemoveDmacHandler` and the bounded handler-record lifetime

- **Status:** Accepted.
- **Date:** 2026-11-10.
- **Context:** After `_DisableDmac` (ADR 0115) the real-title probe stops on `syscall` `0x13` with `$a0 = 5` and `$a1 = 5` at `0x0025A414`. The call site is the PS2SDK `sceSifExitCmd` teardown, which the boot ELF implements at `0x25C3B0`..`0x25C3D8`. The layer already models the registration half (`AddDmacHandler`, ADR 0095) but had no removal path, so the record could only ever be created.
- **Evidence:**
  - **Observed (PS2SDK `ee/kernel/src/sifcmd.c:287-291`):** `sceSifExitCmd` runs `if (sif0_id > 0) { DisableDmac(5); RemoveDmacHandler(5, sif0_id); sif0_id = 0; }`.
  - **Observed (boot ELF, local evidence):** `0x25C3B8` calls the `DisableDmac` wrapper with `$a0 = 5`, then `0x25C3C8` calls the `RemoveDmacHandler` wrapper with `$a0 = 5` and `$a1 = [0x761B54]`, and clears that global at `0x25C3D8`.
  - **Observed (real-title probe):** the stop is `hle_number=0x13`, `$a0 = 5`, `$a1 = 5` — the id the HLE `AddDmacHandler` returned — after `disable_dmac=1`/channel 5/`disabled`.
  - **Observed (PS2SDK `ee/kernel/include/syscallnr.h`):** `__NR_AddDmacHandler` is `0x12`, `__NR_RemoveDmacHandler` is `0x13`.
- **Decision:** Implement `RemoveDmacHandler(channel, id)` against the existing one-record-per-channel table. The id is the HLE-local handle equal to the channel index (ADR 0095). Removal succeeds only when the channel is in `0..9` and its record is in use with that exact id; it then clears the record, drops the handler count and records the removed handler address. A bad channel or a non-matching id changes nothing and returns the layer's documented `-1`. `$v0` is left untouched on success because the kernel's return value is not established by target or public evidence.
- **Scope:** No DMAC interrupt is delivered, so removal cannot race a handler or affect a transfer; it only clears guest-visible HLE state. Chained handlers (`next != 0`) remain unsupported, as they are for registration.
- **Alternatives considered:**
  1. Treat removal as a no-op — rejected: the record would outlive its guest registration and a later `AddDmacHandler` on the same channel would be wrongly refused.
  2. Accept any id on a registered channel — rejected: it would silently remove a handler the guest did not name.
  3. Invent a kernel return value (0 or the removed id) — rejected: neither is observable.
- **Consequences:** The DMAC handler table now has a complete create/remove lifetime, and the `sceSifExitCmd` teardown path can complete. EE DMAC interrupt delivery, the dispatch chain and interrupt-exception delivery remain unimplemented.
- **Validation:** `reps2_hle_syscall_codegen_smoke` adds an add/remove cycle that checks the record, count, removed-handler observation and untouched `$v0`; refusals for an unregistered channel, a wrong id (leaving the record intact) and an out-of-range channel; and a generated-code dispatch of `0x13` that executes no trap.
