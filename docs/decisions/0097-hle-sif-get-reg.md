# ADR 0097: HLE `sceSifGetReg` and the SIF register file

- **Status:** Accepted
- **Date:** 2026-11-08
- **Scope:** EE syscall HLE service table, SIF register model (hardware and kernel software registers), generated-code regression coverage
- **Related target:** Downhill Domination USA SCUS_971.77; observed guest PCs `0x0025C2D0`..`0x0025C33C`, stub `0x0025AAF0`
- **Depends on:** ADR 0092 (SIF0 channel start), ADR 0096 (channel interrupt mask)
- **Correction (ADR 0098):** the statements below that the title polls `SMFLAG & 0x2` are superseded. The retail block loads `lui $s0, 0x2` and tests `$v0 & $s0`, i.e. `SMFLAG & 0x00020000` = PS2SDK `SIF_STAT_CMDINIT`. The title's convention matches PS2SDK.

## Context

ADR 0096 made `EnableDmac` (`0x16`) set the SIF0 interrupt mask, and the real-title probe then advanced to the next boundary: `unhandled_syscall` `$v1 = 0x7a` at `0x0025AAF4` with `$a0 = 0x80000000`. PS2SDK names `0x7a` `sceSifGetReg`.

The observed block is still the SDK's SIF command initialisation, and its shape matches PS2SDK's `sceSifInitCmd`:

- `0x0025C2D4` `sceSifGetReg(0x80000000)` — `SIF_REG_ID_SYSTEM | 0`, which PS2SDK names `SIF_SYSREG_SUBADDR`.
- `0x0025C2DC` `beqz $v0` — when the sub-address is zero the SDK treats the IOP-side SIF command system as *not* initialised and takes a second path.
- that second path (`0x0025C324`..`0x0025C334`) polls `sceSifGetReg(4)` (`SIF_REG_SMFLAG`) in a loop until `$v0 & 2` is non-zero, then reads the hardware `SUBADDR` register and copies it into the software registers with `sceSifSetReg` (`0x79`).

PS2SDK's `ee/kernel/src/sifcmd.c` shows the same two branches in source form: a non-zero `SIF_SYSREG_SUBADDR` means "IOP SIF CMD is already initialized" and the EE sends `SIF_CMD_CHANGE_SADDR`; a zero value leads to `while (!(sceSifGetReg(SIF_REG_SMFLAG) & SIF_STAT_CMDINIT));` followed by adopting the IOP's sub-address.

Both SIF register groups are documented: PS2SDK `ee/include/sifdma.h` numbers the hardware registers 1..4 (`SIF_REG_MAINADDR`/`MSCOM`, `SUBADDR`/`SMCOM`, `MSFLAG`, `SMFLAG`) and the kernel's software registers as `SIF_REG_ID_SYSTEM | index` (`0x80000000`+0..2: SUBADDR, MAINADDR, RPCINIT). PS2SDK `common/include/ee_regs.h` places the hardware registers at `0x1000F200` (`MADDR`), `0x1000F210` (`SADDR`), `0x1000F220` (`MSFLAG`) and `0x1000F230` (`SMFLAG`).

Two facts decide this ADR. First, `sceSifGetReg` must return *something*, and it is a value-returning SDK API, so leaving `$v0` untouched is not an option the way it is for the void `sceSifSetDChain`. Second, the value the retail kernel holds for `SIF_SYSREG_SUBADDR` is an IOP-side buffer address: the observed code stores the returned value into its SIF command structure (`0x0025C2E0` `sw $v0,8($s1)`, and `0x0025C4E4` later loads that same word into an argument of a transfer structure). Inventing such a value would fabricate an IOP-side address that the title programs into SIF DMA structures.

## Evidence

- **Observed (target disassembly):** `0x0025C2D0` `lui $a0,0x8000` / `0x0025C2D4` `jal 0x0025AAF0` (stub `li $v1,0x7a; syscall; jr $ra`) / `0x0025C2DC` `beqz $v0,0x0025C324`; the second path `0x0025C328` `jal 0x0025AAF0` with `0x0025C32C` `li $a0,4`, `0x0025C330` `and $v0,$v0,0x2`, `0x0025C334` `beqz $v0,0x0025C328`, then `0x0025C33C` `sceSifGetReg` with `$a0 = 2` and `0x0025C33C`..`0x0025C360` `sceSifSetReg` writes. Disassembled with the project-local `mips64r5900el-ps2-elf-objdump`.
- **Observed (PS2SDK):** `ee/include/sifdma.h` register numbering and the `SIF_STAT_SIFINIT`/`CMDINIT`/`BOOTEND` flag values; `common/include/ee_regs.h` `A_EE_SBUS_MADDR`/`SADDR`/`MSFLAG`/`SMFLAG`; `ee/include/syscallnr.h` `__NR_sceSifGetReg 0x7a` and `__NR_sceSifSetReg 0x79`; `ee/kernel/src/sifcmd.c` `sceSifInitCmd` showing both branches, including `_sif_cmd_data.iopbuf = sceSifGetReg(SIF_SYSREG_SUBADDR)`.
- **Observed (probe):** before, 3,168,788 instructions stopping at `0x0025AAF4` `unhandled_syscall` `0x7a`. After, the probe executes the full 5,000,000-instruction budget and stops as `budget_exhausted` at `0x0025AAF8`, with `203,526` syscalls, `hle_name=SifGetReg` and `$a0 = 0x00000004` — the SDK's `SMFLAG` poll loop, which never sees the flag it waits for. Extracted ELF SHA-256 `adfda7b7...f0cf0c`, `--discover --hle-syscalls`, both relocated regions.
- **Inferred:** the retail kernel presents a non-zero `SIF_SYSREG_SUBADDR` because the kernel's own SIF initialisation (and the IOP-side SIFCMD it talks to) has already run; the observed title therefore takes the first branch on real hardware.
- **Unknown:** the actual IOP-side address the retail kernel stores, the IOP's `SMFLAG` convention (PS2SDK polls `SIF_STAT_CMDINIT` `0x20000` while this title polls bit `0x2`), and the complete IOP-side SIFCMD handshake.

## Decision

1. Register `SifGetReg` as service `0x7a` (`kEeSyscallSifGetReg`) in the existing sorted HLE table.
2. Add a small SIF register file to the HLE state: the four documented hardware registers (indices 1..4) and the three kernel software registers (`0x80000000`+0..2), with a single accessor defining that address space so a later `sceSifSetReg` shares it.
3. **Every register reads as zero.** REPS2 has no IOP, so the honest model is that the IOP-side SIF command system is not initialised. Reporting a non-zero sub-address would supply an invented IOP-side buffer address that the title stores and later programs into SIF DMA transfer structures.
4. The value is returned in `$v0` with the layer's usual 32-bit sign extension, and the read is recorded (`sif_get_reg_calls`, last register, last value, rejection reason).
5. A register number outside the documented set stops the run as `unhandled_syscall` and records `unknown_register`, rather than answering with an invented value. The observed path never reaches that branch.
6. `sceSifSetReg` (`0x79`) is **not** implemented. It is the next requirement the guest reaches once the IOP handshake it waits for can proceed.

## Validation

`tests/hle_syscall_smoke.cpp` gained four cases and a table-name assertion:

- the observed read (`SIF_SYSREG_SUBADDR`) returns zero with no rejection, all four hardware registers and all three software registers read as zero, and the call counter tracks the reads;
- a value stored in the register file comes back verbatim, proving the service reads the model rather than answering with a constant;
- register number `0` and a number past `RPCINIT` stop with `unhandled_syscall`, record `unknown_register`, and leave `$v0` untouched;
- the dispatch path through generated code resumes with no COP0 exception context.

Local Windows/MSVC verification: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked`, CMake Release build, and `ctest -C Release` (33/33, including `reps2_hle_syscall_codegen_smoke`, now 67 cases). Real-title probe evidence is recorded above.

## Consequences

- The previously unknown service is now modelled, and the trace is *more* informative: instead of stopping on an unimplemented service, the title's own SDK initialisation decides that the IOP-side SIF command system is not up and waits for it.
- **M4 frontier identified:** the target's next requirement is the IOP-side SIF command handshake (the IOP's SIFCMD and the `SMFLAG` flags it publishes), not another single EE syscall. Until the shared runtime models the IOP/SIF conversation, the title parks in this loop. This is the point where M4 work necessarily hands over to the M5 shared-runtime subsystems.
- No fabricated device state was introduced: no IOP-ready flag, no invented sub-address, no SIF DMA.

## Limitations and next evidence

- The register file is host state with no hardware backing: writing it (via a future `sceSifSetReg`) and reading it back only moves values inside the HLE layer.
- The IOP `SMFLAG` bit this title polls (`0x2`) does not match PS2SDK's `SIF_STAT_CMDINIT` (`0x20000`); the title's SDK build and the IOP-side convention are unverified.
- Next highest-value units, in evidence order: (1) an IOP-side SIFCMD model with a documented command/flag protocol, driven by the SIF DMA services the title already reaches toward (`0x79` `sceSifSetReg`, `0x77` `sceSifSetDma`), and (2) `sceSifSetDma` transfer programming behind the existing DMAC owner. Both need their own evidence; neither may be approximated by a success stub.
- No guest-visible boot stage or Playable status is established by this ADR.
