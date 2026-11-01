# ADR 0031: Explicit SYSCALL/BREAK guest-trap statuses

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** `mips` trap-code decoding, `control_ir` trap classification, `control_codegen` status selection, and the runtime `Status` enum

## Context

SYSCALL and BREAK are architectural instructions, not unknown encodings. The
CFG analyzer already stops a block on them because the toolchain cannot deliver
a kernel/BIOS service yet, but the control IR and generated code reported that
stop through the same `Unsupported` terminator and the same
`Status::unsupported_instruction` value as a genuinely unrecognized opcode.
That conflated two different facts:

- the decoder does not know the encoding (a coverage gap), and
- the decoder knows exactly what the guest asked for, but no HLE service layer
  exists (a runtime capability gap).

The distinction matters for diagnostics, for the eventual HLE boundary, and for
not presenting a known trap as a decoder failure. Both instructions also carry
a 20-bit code field in bits 25..6, which the old stop discarded.

The deterministic build manifest is schema v1 and must remain unchanged.

## Decision

### 1. Keep the terminator variant, add a typed trap field

`ControlTerminator::Unsupported` gains `trap: Option<TrapKind>` with
`TrapKind { Syscall, Break }`. The variant name and `unsupported_count()`
semantics stay the same, so a trap is still an explicit stop, the manifest
schema and block counts do not change, and the block is still not translated
further.

Only the analyzer's `Terminator::Unsupported` path classifies the trap by
inspecting the decoded opcode. A scalar-lift failure inside an otherwise
admitted block (for example a decoder-supported but not-yet-lifted COP3 word)
keeps `trap: None` because its opcode is not a known trap.

### 2. Decode the trap code field

`Instruction::trap_code()` returns `(raw >> 6) & 0x000F_FFFF`. The helper is
meaningful only for SYSCALL/BREAK and is documented as such; callers check the
opcode first. The field is not emitted into the generated code because the
trapping PC is preserved, and the code field can be recovered from the word at
that PC.

### 3. Distinct runtime statuses

The runtime `Status` enum appends two values after `invalid_memory_image`:

| value | status | meaning |
| ---: | --- | --- |
| 10 | `syscall` | guest executed SYSCALL |
| 11 | `breakpoint` | guest executed BREAK |

`is_guest_trap(Status)` classifies both. Execution sets `state.pc` to the
trapping instruction and returns; no HLE service is invoked and no fake success
is reported. ERET remains `unsupported_instruction` because it needs exception
state the runtime does not model.

### 4. Codegen selects the status from the trap kind

The scalar/control emitter maps `trap: Some(Syscall)` to `Status::syscall`,
`Some(Break)` to `Status::breakpoint`, and `None` to
`Status::unsupported_instruction`. Nothing else about the emitted dispatcher or
the ABI changes.

### 5. Visibility

`ControlProgram::trap_count()` counts stop blocks with a known trap.
`analyze-elf`/`recompile-elf` print `Guest trap stops (syscall/break)`, and the
synthetic fixture summary prints its guest-trap count. Traps still count once in
`unsupported_count()` so existing SFX totals stay comparable.

## Validation

Rust tests:

- `decodes_syscall_and_break_code_fields` covers code 0, code 1 and the maximum
  20-bit field for both opcodes.
- `syscall_and_break_blocks_carry_guest_trap_kinds` proves the CFG lift assigns
  `TrapKind::Syscall`/`Break` and that `trap_count()` separates them from a
  scalar-lift stop.
- `emits_distinct_status_for_syscall_and_break_traps` proves the generated C++
  contains `Status::syscall` and `Status::breakpoint` and no
  `unsupported_instruction` for those blocks.

The full Rust suite is 161 library + 8 binary tests; `cargo fmt --check` and
`cargo clippy -D warnings` are clean. CTest is 15/15. The synthetic control-flow
fixture now contains 4 guest traps (3 SYSCALL plus 1 BREAK);
`control_flow_smoke.cpp` asserts the three SYSCALL stops and the BREAK stop, and
exercises `is_guest_trap()` for both trap statuses and a non-trap.

Manual verification against `build/isoaudit/Black_USA_iso.elf` (default single
unit, no discovery, `--native-build`):

    entry=0x00100008 status=10 pc=0x001001C8 budget=1000000

`status=10` is `Status::syscall`, and the word at the reported PC is
`0x0000000C` (SYSCALL, code 0). The entry path clears ~0.55 MiB of main RAM
before reaching the trap, which is why the probe needs a budget large enough to
cover that loop; a budget of 4096 truthfully reports
`instruction_budget_exhausted` at the loop instead.

This validates status classification at a real retail trap. It does not deliver
a service, model the exception vector, or establish playability.

## Consequences

- Diagnostics and generated code distinguish a known guest trap from an unknown
  encoding without changing the manifest schema or any block count.
- A future HLE layer has a typed insertion point: read the code field at the
  preserved PC, dispatch the service, and advance the PC explicitly.
- The runtime still has no kernel, exception, or BIOS service surface, so both
  statuses remain terminal stops and every build still records `playable:
  false`.
- The code field is exposed by the decoder but is not yet surfaced in emitted
  code or the manifest; adding it requires a deliberate artifact/version
  decision rather than an incidental schema change.