# ADR 0096: HLE `_EnableDmac` sets the D_STAT channel interrupt mask

- **Status:** Accepted
- **Date:** 2026-11-08
- **Scope:** EE syscall HLE service table, D_STAT interrupt-mask register effect, generated-code regression coverage
- **Related target:** Downhill Domination USA SCUS_971.77; observed guest PCs `0x0025C2C8`, `0x0025B068`, stub `0x0025A440`
- **Depends on:** ADR 0079 (D_STAT contract), ADR 0091 (DMAC register file and boot state), ADR 0095 (DMAC handler records)

## Context

ADR 0095 recorded the SIF0 completion handler, and the real-title probe then advanced to the next boundary: `unhandled_syscall` `$v1 = 0x16` at `0x0025A444`. PS2SDK names `0x16` `_EnableDmac`.

The observed call site is a small SDK wrapper: the SIF initialisation block calls `0x0025B068` with `$a0 = 5` (`0x0025C2C8`, delay slot `sw $v0,6996($v1)` storing the handler id). The wrapper reads CP0 `Status`, conditionally runs a critical-section helper, and then calls stub `0x0025A440` — `li $v1,0x16; syscall; jr $ra` — with `$a0 = 5` still in place, and propagates the result to its caller. The observed caller does not test that result: the following instruction overwrites `$v0` with the next call.

The guest-visible effect of enabling a DMAC channel interrupt is a D_STAT register write. PS2Tek's EE register map states that `D_STAT` bits 16..25 are the per-channel interrupt mask, "1 = enabled, write 1 to reverse" (write-one-to-toggle), and that "INT1 is asserted when (status & mask) != 0". Two consequences follow: the bit must be *set* to enable the channel, and because a write of one toggles the bit, enabling an already-enabled channel requires reading the register first and writing nothing.

That the kernel's idle state has the mask clear is consistent with the SDK's own `dma_reset` clearing `D_STAT` (ADR 0091) and with this call ordering: the title registers the handler and then asks for the channel interrupt.

## Evidence

- **Observed (target disassembly):** `0x0025C2C8` `jal 0x0025B068` with `$a0 = 5`; inside the wrapper at `0x0025B090`/`0x0025B098` the `0x16` stub `0x0025A440` is called and the result propagated as `$v0`; the caller at `0x0025C2D0` overwrites `$v0` without testing it. Disassembled with the project-local `mips64r5900el-ps2-elf-objdump`.
- **Observed (PS2SDK):** `ee/include/kernel.h` declares `s32 _EnableDmac(s32 channel)` and `ee/include/syscallnr.h` defines `__NR__EnableDmac 0x16` (with `0x17` `_DisableDmac`).
- **Observed (PS2Tek EE register map):** `D_STAT` bits 16..25 are the per-channel interrupt mask with write-one-to-toggle; bit 16+channel belongs to that channel; the assertion rule is `(status & mask) != 0`. The map names `0x1000E000` `D_CTRL`, `0x1000E010` `D_STAT`, `0x1000E020` `D_PCR` (priority control) and gives neither of the latter two any interrupt-mask field, so the mask is the D_STAT field.
- **Observed (existing model):** ADR 0079 already implements the D_STAT mask bits 16..25 as write-one-to-toggle and exposes them in the readable mask.
- **Observed (probe):** before, 3,168,763 instructions stopping at `0x0025A444` `unhandled_syscall` `0x16`. After, 3,168,788 instructions (25 more) stopping at `0x0025AAF4` `unhandled_syscall` `$v1 = 0x7a` with `$a0 = 0x80000000`. Extracted ELF SHA-256 `adfda7b7...f0cf0c`, `--discover --hle-syscalls`, both relocated regions, 5,000,000 budget.
- **Inferred:** the SDK wrapper's propagation of `$v0` shows the kernel has some return value, but neither the target nor PS2SDK establishes what it is, and the observed caller discards it.
- **Unknown:** whether the kernel's `_EnableDmac` performs any additional work (critical-section or D_CTRL state), and how SIF0 completion is expected to reach the registered handler.

## Decision

1. Register `EnableDmac` as service `0x16` (`kEeSyscallEnableDmac`) in the existing sorted HLE table.
2. The handler reads `D_STAT` through the DMAC owner, computes the channel's mask bit `1 << (16 + channel)`, and writes that bit only when it is currently clear, using the owner's documented write-one-to-toggle semantics. This sets the mask without a read-modify-write race and without ever toggling an enabled channel back off.
3. An out-of-range channel (`>= 10`) is refused with the layer's documented negative failure value `-1` and records `invalid_channel`; nothing is written.
4. When the DMAC register state is unknown, the service returns `unsupported_memory_access` — the same refusal the DMAC owner produces — instead of guessing a value. It records `unknown_register_state`.
5. On success the service does not write `$v0`: the kernel's return contract is not established by target or public evidence, so the layer does not invent a result. It records `enabled`, or `already_enabled` when the channel mask was already set.
6. No interrupt is delivered. The write leaves the register agreeing with what the real kernel would present; interrupt delivery remains unimplemented and is recorded as such.

## Validation

`tests/hle_syscall_smoke.cpp` gained four cases and a table-name assertion:

- enabling channel 5 sets `D_STAT` bit 21 while an unrelated status field and an unrelated channel mask survive the read-modify-write, and `$v0` is untouched;
- a second enable of the same channel reports `already_enabled` and leaves the bit set (not toggled off);
- an out-of-range channel is refused with `-1` and changes no register; an uninitialized runtime stops with `unsupported_memory_access` and records `unknown_register_state`;
- the dispatch path through generated code sets the mask bit and resumes with no COP0 exception context.

Local Windows/MSVC verification: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked`, CMake Release build, and `ctest -C Release` (33/33, including `reps2_hle_syscall_codegen_smoke`, now 63 cases). Real-title probe evidence is recorded above.

## Consequences

- The observed SIF initialisation continues past the interrupt enable. The next requirement is identified precisely: `sceSifGetReg` (`0x7a`) at `0x0025AAF4` with `$a0 = 0x80000000`.
- The runtime still cannot deliver a DMAC interrupt, so the mask now set by this service causes nothing: the registered SIF0 handler is never invoked.

## Limitations and next evidence

- `_DisableDmac` (`0x17`) is not implemented.
- The mask bit is set but never acted on: DMAC interrupt delivery, COP0 interrupt entry and the SIF command handshake remain unimplemented, so this path cannot reach interrupt-driven command processing yet. Modelling that needs its own evidence and must not be approximated.
- Next: implement `sceSifGetReg` (`0x7a`) at the new boundary, then `sceSifSetReg` (`0x79`) and `sceSifSetDma` (`0x77`) as the trace requires.
- No guest-visible boot stage or Playable status is established by this ADR.
