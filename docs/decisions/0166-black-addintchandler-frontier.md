# ADR 0166: The Black `AddIntcHandler` frontier — stop at syscall `0x10` instead of registering a handler nothing can deliver

- **Status:** Accepted (observation and fail-closed decision; no code change).
- **Date:** 2026-11-19.
- **Scope:** `docs/RESEARCH.md`, this ADR, and the committed Black frontier
  record. `runtime/include/reps2/runtime/ee_hle.hpp` is unchanged: syscall `0x10`
  is still unhandled and the engine still stops.
- **Relates to:** ADR 0164 (runtime ABI layout contract), ADR 0073/0129 (Timer 3),
  ADR 0105 (EE DMAC interrupt line is not an INTC source).

## Context

Once the two runtime-copied regions are declared as relocated code, the Black
(USA) probe no longer stops at `0x80076000`. It executes the copied syscall
handler and reaches a **different** frontier: an unhandled EE syscall `0x10`.
This ADR records what the guest asked for, from the instruction words, and
decides what REPS2 may do about it. The decision matters because the cheapest
way to "advance" the trace here is to return a synthetic handler id, and that
would be invention.

## Observed

Title and artifacts:

- Disc serial `SLUS_213.76`; ISO SHA-256
  `65768cade88bfbaf566b67a3ef7a0bda5a9669779833914ffe7cd73a3e45cccd`.
- Boot ELF SHA-256
  `141b05209570458af9c8e27ab1911f258da6353909d813cd572df026254ffe4c`,
  ELF32, one `PT_LOAD` with `p_vaddr=0x00100000`, `p_offset=0x00001000`,
  `p_filesz=0x0030E580`, `p_memsz=0x0039BFBC`, flags R|W|X.
- Therefore the initial 32 MiB EE main-RAM image holds image address `A` at ELF
  file offset `A - 0x000FF000`. Every address below is an **image address**;
  the file offset is the address minus `0x000FF000`.
- The initial image digest is `16c34a61...ba72` in both configurations below,
  so declaring relocated regions changes only analysis, not image content.

Configuration (ABI 19, `--discover --tu-count 8 --probe-budget 5000000
--max-batches 16 --hle-syscalls`, relocated regions
`0x80076000:0x003D7A00:0x740` and `0x00082000:0x003D8140:0x28`, frontend
SHA-256 `abe928b2...b24572`, engine SHA-256 `ea8d5117...b46c30`):

| Run | Relocated regions | Result |
| --- | --- | --- |
| `black-abi19-run1` | none | `guest_stopped` / `unresolved_control_flow` at `pc=0x80076000`, 479,362 instructions, syscalls 15 seen / 10 handled |
| `black-abi19-run3` | both | `guest_stopped` / `unhandled_syscall` at `pc=0x00367554`, 482,824 instructions, syscalls 27 seen / 16 handled |

`black-abi19-run3`'s `run-report.json` SHA-256 is
`b0a73082e3cf5dc5914f6da4b45838b2e523270d89ad34990bd826011b8af696`. A
follow-up probe of the same configuration (`run4`, asserted commit differing)
reproduced the identical result block, so the stop is repeatable.

Instruction words, verbatim, from the initial image (all file-backed inside the
single `PT_LOAD`):

The call site — region `0x0036DC40..0x0036DC5F`, SHA-256
`0831882df72a322dcff0c1903575a4e656b6fa8a54a6356d793aebe8df7c32ee`:

| Address | Word | Instruction |
| --- | --- | --- |
| `0x0036DC40` | `0x3C050037` | `lui $5, 0x0037` |
| `0x0036DC44` | `0x2404000B` | `addiu $4, $0, 0x000B` |
| `0x0036DC48` | `0x24A5DFF0` | `addiu $5, $5, 0xDFF0` |
| `0x0036DC4C` | `0x0000302D` | `daddu $6, $0, $0` |
| `0x0036DC50` | `0x0C0D9D54` | `jal 0x00367550` |
| `0x0036DC54` | `0x0000382D` | `daddu $7, $0, $0` |
| `0x0036DC58` | `0x0040182D` | `daddu $3, $2, $0` |
| `0x0036DC5C` | `0x04630004` | `bgezl $3, 0x0036DC70` |

The target — the syscall thunk table region `0x00367540..0x0036759F`, SHA-256
`4d6d555300583cf7c9ce2052df9049f4dbf9f7666d75b6cb594009bd2f1c28f2`. Each entry
is `addiu $3, $0, <number>; syscall; jr $ra; nop`:

| Address | Word | Instruction |
| --- | --- | --- |
| `0x00367540` | `0x24030010` | `addiu $3, $0, 0x0010` |
| `0x00367550` | `0x24030010` | `addiu $3, $0, 0x0010` |
| `0x00367554` | `0x0000000C` | `syscall` |
| `0x00367558` | `0x03E00008` | `jr $ra` |
| `0x00367560` | `0x24030011` | `addiu $3, $0, 0x0011` |
| `0x00367570` | `0x24030012` | `addiu $3, $0, 0x0012` |
| `0x00367580` | `0x24030012` | `addiu $3, $0, 0x0012` |
| `0x00367590` | `0x24030013` | `addiu $3, $0, 0x0013` |

The registered handler — entry `0x0036DFF0..0x0036E02F`, SHA-256
`731681381729bbbfda5a2094323aa53ead07162bee05457975fc5f7bd7654ab8`:

| Address | Word | Instruction |
| --- | --- | --- |
| `0x0036DFF0` | `0x27BDFF70` | `addiu $29, $29, 0xFF70` |
| `0x0036DFF4` | `0x3C031000` | `lui $3, 0x1000` |
| `0x0036DFF8` | `0xFFB60060` | `sd $22, 0x0060($29)` |
| `0x0036DFFC` | `0x34631010` | `ori $3, $3, 0x1010` |
| `0x0036E000` | `0xFFBF0080` | `sd $31, 0x0080($29)` |
| `0x0036E004` | `0x00C0B02D` | `daddu $22, $6, $0` |

The runtime side (observed in `runtime/include/reps2/runtime/ee_hle.hpp` and
`ee_intc.hpp`): `kEeSyscallAddDmacHandler = 0x12` and
`kEeSyscallRemoveDmacHandler = 0x13` are served; there is no service constant
for `0x10` or `0x11`. `EeIntcState` models `INTC_STAT` (`0x1000F000`),
`INTC_MASK` (`0x1000F010`), `raise_source` and `line_asserted`, but holds **no
handler registry**, and `kEeIntcSourceTimer2 = 11` is named without any timer
model behind it. Timer 3 (`0x10001800`/`0x10001810`/`0x10001820`) is the only
modelled timer and it raises no interrupt.

## Derived

1. `$5` becomes `0x0036DFF0`: `lui $5, 0x0037` gives `0x00370000`, and
   `addiu $5, $5, 0xDFF0` adds the sign-extended `-0x00002010`. The value is
   exactly the entry point of the second function above, which is therefore the
   handler argument.
2. The call at `0x0036DC50` passes `$4=0x0000000B`, `$5=0x0036DFF0`,
   `$6=0`, `$7=0` and jumps to `0x00367550`, whose first instruction loads
   `0x10` into `$3` in front of the `syscall` word at `0x00367554`. The engine's
   recorded `pc` is `0x00367554`, i.e. the syscall word that ended the batch.
3. `0x0036DC58`/`0x0036DC5C` move the return value `$2` to `$3` and branch on
   its sign, so **the guest inspects the handler id**: a negative result is a
   failure path, a non-negative result is success. REPS2 has never observed
   either path, because it stops at the syscall.
4. Two distinct thunks load `0x10` (`0x00367540` and `0x00367550`), and two load
   `0x12` (`0x00367570`, `0x00367580`). The table is therefore not
   one-entry-per-number; it is a sequence of library wrappers, and the guest
   used the second `0x10` wrapper.

## Inferred

- `0x10` is `AddIntcHandler` and `0x11` is `AddIntcHandler2`, per the ps2sdk
  numbering REPS2 already recorded for this boot path (`docs/RESEARCH.md`,
  service-number line citing `ee/kernel/include/kernel.h` and `syscallnr.h`).
  The observed argument set `(0xB, handler, 0, 0)` matches a registration
  signature with an optional `next` argument; the two zeroed registers make the
  3-argument and 4-argument readings indistinguishable here.
- If `0xB` is an INTC source number, REPS2's own mapping
  (`kEeIntcSourceTimer2 = 11`) names **TIMER2**. The handler's first block
  builds `0x10001010` in `$3`; REPS2 models no EE register at that address, so
  the handler body cannot be executed faithfully today either.

## Unknown

- Whether the guest would proceed at all if a handler id were returned: the
  registration is followed by a wait for an interrupt that no REPS2 subsystem
  raises, and neither the wait nor its outcome has been observed.
- What the handler does with `0x10001010`, and what the EE register map names at
  that address. Not established by any source used here.
- Which wrapper (3-argument or 4-argument) the guest used, and whether the
  duplicate thunks correspond to `AddIntcHandler`/`AddIntcHandler2` or to
  another intra-library distinction.
- The full caller chain. The recorded instruction trace is a 16-entry window of
  the final instructions (`0x0036DC40..0x0036DC54` plus the thunk), not a stack
  or call graph; nothing here establishes who called `0x0036DC40`.

## Correction (2026-11-19): `0x10001010` is the Timer 2 mode register

The third Unknown bullet above ("what the EE register map names at that
address") is answered by REPS2's own timer layout, and the answer changes what
the next slice is:

- **Observed:** `runtime/include/reps2/runtime/ee_state.hpp` models Timer 3 at
  `0x10001800` (count), `0x10001810` (mode) and `0x10001820` (compare), i.e.
  stride `0x800`. `0x10001010` is therefore `0x10001810 - 0x800`, the **Timer 2
  mode** register, and `0x10001000` is the Timer 2 count register.
- **Observed:** a scan of the whole `.text` range of the Black image
  (`0x00100000..0x0030E580`, word-aligned, `lui $x,0x1000` followed within four
  instructions by `ori`/`addiu` on the same register) finds eleven sites that
  build a Timer 2 or Timer 3 address: `0x0036DA34` → `0x10001810`,
  `0x0036DC78`, `0x0036DD20`, `0x0036DDCC`, `0x0036DFF4`, `0x0036E050`,
  `0x0036E1B4` → `0x10001010`, and `0x0036DE94`, `0x0036E048`, `0x0036E1AC`,
  `0x0036E298` → `0x10001000`. The handler registered by the `0x10` call is one
  of the eleven (`0x0036DFF4`), and the caller's own continuation is another
  (`0x0036DC78`).
- **Observed words** in the caller's continuation, immediately after the
  registration returns: `0x0036DC78` `3C031000` (`lui $3,0x1000`), `0x0036DC80`
  `34631010` (`ori $3,$3,0x1010`), `0x0036DC88` `8C700000` (`lw $16,0($3)`),
  `0x0036DC90` `02021024` (`and $2,$16,$2` with `$2=0xFFFFFFFC` from
  `0x0036DC84`/`0x0036DC8C`), `0x0036DC94` `00548025` (`or $16,$2,$20`),
  `0x0036DC98` `36100300` (`ori $16,$16,0x0300`), `0x0036DC9C` `32020080`
  (`andi $2,$16,0x0080`), `0x0036DCA0` `14400005` (`bne $2,$0,+5`).
- **Derived:** the sequence is a read-modify-write of the Timer 2 mode word:
  clear the low two bits, OR in the caller's flag, set bits 8-9, and branch on
  bit 7. Together with `0x0B` as the first argument, the registration is a
  Timer 2 interrupt handler registration, not an unrelated handler that merely
  touches a timer register.
- **Consequence for the decision:** the handler is not blocked by an unknown
  register address. It is blocked by the two things the Decision already names:
  an INTC handler registry and a Timer 2 model. Any future implementation still
  needs the exact mode-bit semantics the library expects, which this correction
  does not establish — bits 8-9 and bit 7 are set and tested here, but their
  hardware meaning is not derived from this evidence.

## Decision

1. **Do not implement syscall `0x10` or `0x11` in this change.** Returning a
   synthetic handler id is exactly the substitution this repository forbids: it
   turns an unsupported operation into a successful one so a trace advances.
   The honest status remains `unhandled_syscall`.
2. If the service is implemented later, it needs three separate evidenced
   pieces, not one: an INTC handler registry with the same id/ordering semantics
   the kernel exposes (an `EeState` layout change, therefore a runtime ABI
   revision under ADR 0164), a TIMER2 model whose counter/mode/compare semantics
   are established from the title's own programming sequence, and INTC delivery
   through the COP0 IP2 line — note that ADR 0105 already fixed the analogous
   mistake of routing DMAC completion through the INTC.
3. Record the frontier: the relocated Black path is no longer blocked by
   copied code; it is blocked by an EE interrupt-handler registration that REPS2
   cannot honour.

## Consequences

- The next Black slice is a device-model slice (INTC + TIMER2), not a compiler
  slice. It is larger than the relocated-code declaration that preceded it, and
  it must start with evidence for the guest's TIMER2 programming, not with a
  registry data structure.
- Until then the committed Black record documents the deepest reproducible stop
  for this title and states why it is not a boot stage.

## Validation

- Instruction words, region digests and the image bias were re-read from the
  probe's own `boot.ram.bin` and cross-checked byte-for-byte against the ELF
  (`ram[A] == elf[A - 0xFF000]` for every address cited above, including
  `0x00367540`, `0x00367550`, `0x0036DC40`, `0x0036DC50`, `0x0036DFF0`,
  `0x003D7A00`, `0x003D8140`).
- The result block comes from the probe's `frontier.json`/`run-report.json`, not
  from a retyped constant; the record is validated by
  `python tools/frontier/frontier.py validate`.
- The runtime claims (`kEeSyscall*`, `EeIntcState`, Timer 3 addresses) were
  re-read from the headers named above. No test result is claimed because no
  code changed.