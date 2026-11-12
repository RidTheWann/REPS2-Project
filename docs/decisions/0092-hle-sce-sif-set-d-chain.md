# ADR 0092: HLE `sceSifSetDChain` starts the SIF0 channel

- **Status:** Accepted
- **Date:** 2026-11-08
- **Scope:** EE syscall HLE service table, SIF0 DMA channel start, generated-code regression coverage
- **Related target:** Downhill Domination USA SCUS_971.77; observed guest PCs `0x0025C26C`..`0x0025C2B8`
- **Depends on:** ADR 0091 (DMAC register file and HLE boot state), ADR 0079 (D_STAT contract)

## Context

ADR 0091 cleared the `D_STAT`/SIF0 read so the real-title probe no longer stopped at `0x0025C274`, and it identified the next requirement precisely: syscall `$v1 = 0x78`, which PS2SDK names `sceSifSetDChain`. The probe stopped there as `unhandled_syscall` because the HLE layer had no handler, and the layer never invents a result for a service it does not implement.

The observed guest code is the SDK's SIF command initialisation. Reading the extracted ELF shows the exact sequence the boundary sits inside: read `D_STAT` (`0x1000E010`), test the SIF0 status bit `0x20`, conditionally write the bit back (W1C acknowledge), read SIF0 `CHCR` (`0x1000C000`), test `CHCR_STR` `0x100`, and only when the channel is *not* running call the `0x78` stub at `0x0025AAC0` (`li $v1,0x78; syscall; jr $ra`). Immediately afterwards the same block registers a DMAC handler for channel 5, which shows the payload path continues once the channel is started.

`sceSifSetDChain` is declared `void` in PS2SDK, takes no arguments, and exists to start the SIF0 channel the kernel has already been programmed with. The kernel-side chain builder itself is not observable in the target: the ROM kernel keeps its own command buffer and tag chain, and no target or public evidence establishes which `CHCR` word it writes. What the guest does depend on is the guest-visible effect: after the call the channel is running and then reports completion.

## Evidence

- **Observed (target disassembly):** `0x0025C26C` `lui $v0,0x1000` / `0x0025C270` `ori $v0,$v0,0xE010` / `0x0025C274` `lw $v1,0($v0)` / `0x0025C278` `andi $v1,$v1,0x20` / `0x0025C27C` `beqz $v1` / `0x0025C288` `sw` of `0x20` back to `0x1000E010` / `0x0025C290` `ori $v0,$v0,0xC000` / `0x0025C294` `lw` / `0x0025C298` `andi $v1,$v1,0x100` / `0x0025C29C` `bnez $v1` / `0x0025C2A4` `jal 0x0025AAC0`. Disassembled with the project-local `mips64r5900el-ps2-elf-objdump`.
- **Observed (target stub):** `0x0025AAC0` is `li $v1,0x78; syscall; jr $ra`. The following call site, `0x0025C2B8` `jal 0x0025A3F0`, reaches stub `0x0025A3F0` (`li $v1,0x12`) with `$a0 = 5` and `$a1 = 0x0025C5F8`.
- **Observed (PS2SDK):** `ee/include/sifcmd.h` declares `extern void sceSifSetDChain(void);`; `ee/include/syscallnr.h` defines `__NR_sceSifSetDChain 0x78` and `__NR_AddDmacHandler 0x12`. `ee/kernel/src/sifcmd.c` (master) shows `sceSifInitCmd` reading `DMAC_COMM_STAT` `0x1000e010`, acknowledging `STAT_SIF0` `0x20`, and calling `sceSifSetDChain()` only when `!(DMAC_SIF0_CHCR & CHCR_STR)` — the same shape as the retail sequence.
- **Observed (probe):** with `0x78` serviced, the regenerated probe (extracted ELF SHA-256 `adfda7b7...f0cf0c`, `--discover --hle-syscalls`, both relocated regions, 5,000,000 budget) executes 3,168,726 guest instructions — 9 more than the ADR 0091 run — and stops at `0x0025A3F4` as `unhandled_syscall` with `$v1 = 0x12`. The syscall ring records `pc=0x0025AAC4 number=120 path=host_service`, so the service ran rather than refusing.
- **Inferred:** the kernel's own SIF0 chain buffer and the exact `CHCR`/tag words it programs are not observable in the target or in public sources. This ADR therefore models only the start-and-complete effect, not the transfer content.
- **Non-normative cross-check:** PCSX2 does not HLE the EE kernel (it runs the BIOS kernel), so it offers no cross-check for this service.

## Decision

1. Register `sceSifSetDChain` as service `0x78` (`kEeSyscallSifSetDChain`) in the existing sorted HLE table, under the name `SifSetDChain`.
2. The handler requires the SIF0 channel control register and `D_STAT` to have a known state. When they do not, it returns `unsupported_memory_access` — the same fail-closed refusal the DMAC owner itself produces — instead of starting a channel whose completion could not be represented.
3. When `CHCR_STR` is already set the handler returns `ok` with no side effect: a second start of a running channel has no observable effect.
4. Otherwise it writes `CHCR5` with `CHCR_STR` raised through the DMAC owner, which publishes the SIF0 completion event to `D_STAT` and self-clears `STR` (`ADR 0091` semantics).
5. The handler does not write `$v0`. The SDK declares the entry `void`, so no result register contract exists and the layer does not invent one.
6. The kernel chain builder, the command buffer, DMA tag traversal, IOP handshake, and `sceSifSetDma` (`0x77`) are **not** implemented here.

## Validation

`tests/hle_syscall_smoke.cpp` gained four cases and a table-name assertion:

- an idle channel start sets the `D_STAT` SIF0 event, leaves `CHCR_STR` clear, and leaves `$v0` untouched;
- a channel that is already running is left alone and no completion event is invented;
- an uninitialized runtime refuses with `unsupported_memory_access` and mutates nothing;
- the dispatch path through generated code resumes at the continuations with no COP0 exception context and records one handled call.

Local Windows/MSVC verification: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked` (327 pass), CMake Release build, and `ctest -C Release` (33/33, including `reps2_hle_syscall_codegen_smoke`). Real-title probe evidence is recorded above.

## Consequences

- The observed SIF initialisation continues past the channel start: the probe advances 9 instructions to the next boundary, `AddDmacHandler` (`0x12`, channel 5).
- The service is an HLE model of the kernel's guest-visible effect. It performs no data transfer and produces no IOP-side effect, and it is documented as such rather than presented as a working SIF DMA implementation.

## Limitations and next evidence

- The `CHCR5` start is still modelled as an immediately completed zero-length transfer. No quadwords move and no SIF/IOP channel state exists behind it.
- DMAC interrupt delivery is not modelled. The handler the guest registers next (`AddDmacHandler`, channel 5) will not be invoked by the completion event, so a path that waits for interrupt-driven command processing will not progress without further work.
- Next: implement `AddDmacHandler` (`0x12`) for the new boundary at `0x0025A3F4`, then the SIF register services (`0x79` `sceSifSetReg`, `0x7a` `sceSifGetReg`) and `sceSifSetDma` (`0x77`) as the trace requires, each behind the same fail-closed discipline.
- No guest-visible boot stage or Playable status is established by this ADR.
