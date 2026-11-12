# ADR 0095: HLE `AddDmacHandler` records a DMAC channel handler

- **Status:** Accepted
- **Date:** 2026-11-08
- **Scope:** EE syscall HLE service table, DMAC handler records, generated-code regression coverage
- **Related target:** Downhill Domination USA SCUS_971.77; observed guest PCs `0x0025C2B0`..`0x0025C2CC`, stub `0x0025A3F0`
- **Depends on:** ADR 0091 (DMAC register file and boot state), ADR 0092 (SIF0 channel start)

## Context

ADR 0092 made `sceSifSetDChain` (`0x78`) start the SIF0 channel, and the real-title probe then advanced to the next boundary: `unhandled_syscall` `$v1 = 0x12` at `0x0025A3F4`. PS2SDK names `0x12` `AddDmacHandler`.

The observed call site continues the same SDK SIF initialisation block:

- `0x0025C2B0` `li $a0,5` — DMAC channel 5, which is SIF0.
- `0x0025C2B4` `addiu $a1,$a1,-14856` — `$a1 = 0x0025C5F8`, the handler.
- `0x0025C2B8` `jal 0x0025A3F0` with delay slot `0x0025C2BC` `move $a2,zero` — `next = 0`.
- `0x0025C2CC` `sw $v0,6996($v1)` — the returned id is stored in a global.
- `0x0025C2C8` `jal 0x0025B068` — a wrapper that reads CP0 `Status` and calls stub `0x0025A440` (`li $v1,0x16`, `_EnableDmac`) with `$a0 = 5`.

The handler at `0x0025C5F8` processes the SIF command packet: it reads a byte count out of the kernel command buffer pointer at `0x761B58`, computes the quadword count and copies quadwords. The path therefore expects SIF0 completion processing, which the kernel drives from the DMAC interrupt.

The kernel's handler bookkeeping is not observable from the target: handler ids, the per-channel chain, and the exact moment the channel interrupt is enabled are all internal. PS2SDK exposes the shape only — `s32 AddDmacHandler(s32 channel, s32 (*handler)(s32 channel), s32 next)`, with `RemoveDmacHandler(channel, handler_id)` taking the returned id back.

## Evidence

- **Observed (target disassembly):** the call site and stub listed above, disassembled with the project-local `mips64r5900el-ps2-elf-objdump`. Stub `0x0025A3F0` is `li $v1,0x12; syscall; jr $ra`; stub `0x0025A440` is `li $v1,0x16; syscall; jr $ra`.
- **Observed (PS2SDK):** `ee/include/kernel.h` declares `s32 AddDmacHandler(s32 channel, s32 (*handler)(s32 channel), s32 next)` (third parameter `next`, not an argument pointer; the 4-argument form is `AddDmacHandler2`) and `s32 RemoveDmacHandler(s32 channel, s32 handler_id)`. `ee/include/syscallnr.h` defines `__NR_AddDmacHandler 0x12`, `__NR_AddDmacHandler2 0x13`, `__NR_RemoveDmacHandler 0x14`, `__NR__EnableDmac 0x16`.
- **Observed (channel count):** D_STAT channel status bits are 0..9 and PS2SDK's own `dma_reset` loops the ten channels (ADR 0091), so ten channels is the modelled bound.
- **Observed (probe):** before, 3,168,726 instructions stopping at `0x0025A3F4` `unhandled_syscall` `0x12`. After, 3,168,763 instructions (37 more) stopping at `0x0025A444` `unhandled_syscall` `$v1 = 0x16` with `$a0 = 5`, `$a1 = 0x0025C5F8`. Extracted ELF SHA-256 `adfda7b7...f0cf0c`, `--discover --hle-syscalls`, both relocated regions, 5,000,000 budget.
- **Inferred:** the kernel's handler-id values and its chain order are unknown. `$v0` at the new boundary is `1` only because the caller's own wrapper executes `lui $v0,0x1` while testing CP0 `Status`; it is not the id this service returned.
- **Unknown:** whether the kernel enables the channel interrupt inside `AddDmacHandler`, and how SIF0 completion is expected to reach the registered handler.

## Decision

1. Register `AddDmacHandler` as service `0x12` (`kEeSyscallAddDmacHandler`) in the existing sorted HLE table under the name `AddDmacHandler`.
2. Store one handler record per DMAC channel in a bounded 10-entry table. A record holds the handler address, `next`, and the registering guest PC.
3. Return ids that are HLE-local handles equal to the channel index. The kernel's own id values are not observable from the target, so reproducing them would be a guess; the layer documents the handle as local, exactly as it does for thread and semaphore ids (ADRs 0075/0066).
4. Refuse, with the documented negative failure value `-1` and a recorded reason, when: the channel is outside 0..9 (`invalid_channel`); the handler is null, misaligned, or not resolvable in EE main RAM (`invalid_handler`); `next` is non-zero (`chained_handler_unsupported`); or the channel already holds a handler (`channel_already_registered`). A refusal creates nothing and changes nothing.
5. Deliver no DMAC interrupt and touch no interrupt mask. The record is created state only: the handler is never invoked, so interrupt-driven SIF command processing cannot progress yet. This limitation is recorded rather than papered over by enabling an interrupt whose delivery does not exist.

## Validation

`tests/hle_syscall_smoke.cpp` gained three cases and a table-name assertion:

- the observed registration (`channel 5`, handler `0x0025C5F8`, `next 0`) stores the record, returns `5`, and records the boundary metadata;
- refusals cover an out-of-range channel, a null and a misaligned handler, a non-zero `next`, and a duplicate registration — each returning `-1`, creating nothing, and leaving an existing record byte-for-byte unchanged;
- the dispatch path through generated code resumes after the trap with no COP0 exception context.

Local Windows/MSVC verification: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked`, CMake Release build, and `ctest -C Release` (33/33, including `reps2_hle_syscall_codegen_smoke`, now 59 cases). Real-title probe evidence is recorded above.

## Consequences

- The observed SIF initialisation continues past handler registration. The next requirement is identified precisely: `_EnableDmac` (`0x16`) at `0x0025A444`.
- Handler records are guest-visible state without behaviour: no code runs and no interrupt is raised.

## Limitations and next evidence

- One handler per channel is modelled; the kernel's per-channel chain is refused rather than approximated, so a title that chains handlers on one channel stops at `unhandled_syscall`/`-1` instead of getting an invented chain.
- `RemoveDmacHandler` (`0x14`) and `AddDmacHandler2` (`0x13`) are not implemented.
- DMAC interrupt delivery, interrupt-mask manipulation and thread scheduling remain unimplemented, so the registered SIF handler is never called. That is the structural gap behind this path, and it will need explicit evidence before it is modelled.
- Next: implement `_EnableDmac` (`0x16`) at the new boundary, then the SIF register services (`0x79` `sceSifSetReg`, `0x7a` `sceSifGetReg`) and `sceSifSetDma` (`0x77`) as the trace requires.
- No guest-visible boot stage or Playable status is established by this ADR.
