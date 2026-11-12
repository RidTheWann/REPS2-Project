# ADR 0129: Correct the Downhill Timer 3 MMIO frontier

- **Status:** Accepted.
- **Date:** 2026-11-11.
- **Scope:** M4 real-title investigation and EE Timer 3 MMIO boundary.

## Context

After PR #81 admitted control-flow targets in declared relocated-code regions and scanned those regions for function roots, the Downhill Domination USA probe moved from an unresolved CFG boundary to an MMIO access in the copied kernel code. PR #82 added a 32-lane GPR snapshot to the engine run report to identify the effective address.

The PR #82 evidence text described physical address `0x10001800` as an "unmodelled DMAC-area register". That classification is incorrect.

## Evidence

- **Observed (Downhill Domination USA, SCUS_971.77; PR #82 head `d4496a7`; Windows x64/MSVC):** execution stopped at guest PC `0x800761A4` after 3,890,274 guest instructions. The instruction is `lw $20, 0($2)`; the report records `$2 = 0xFFFFFFFFB0001800`.
- **Derived:** the low 32-bit guest address is `0xB0001800`, the EE KSEG1 alias of physical `0x10001800`.
- **Public register map:** PS2SDK `ee_regs.h` identifies `0x10001800` as `T3_COUNT`, `0x10001810` as `T3_MODE`, and `0x10001820` as `T3_COMP`. The PS2TEK EE timer documentation describes a 16-bit counter controlled by `Tn_MODE` and identifies Timer 3 as reserved by BIOS alarm functions.
- **Existing REPS2 behavior:** ADR 0073 already models only the Timer 3 mode word at `0x10001810`: reset value zero and retention of its documented writable bits. Timer counting, compare matching, overflow and interrupt delivery are not implemented. `T3_COUNT` and `T3_COMP` remain deliberately unsupported.

References:
- PS2SDK EE register definitions: https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/ee_regs.h
- PS2SDK Timer API: https://ps2dev.github.io/ps2sdk/timer_8h_source.html
- PS2TEK, EE Timers: https://israpps.github.io/ps2tek/

## Decision

1. Correct the frontier classification: `0x10001800` is EE Timer 3 Count (`T3_COUNT`), not a DMAC register and not main RAM.
2. Preserve the existing fail-closed behavior for `T3_COUNT` and `T3_COMP`. Do not return zero, a host clock value, or a synthetic counter merely to advance the title.
3. Before implementing Timer 3, establish the target's observed `T3_MODE` and `T3_COMP` values, all writes to those registers, counter writes, read/write widths, and whether Timer 3 is enabled at the faulting path.
4. Any future timer model must define a deterministic guest-time source and compare/overflow semantics, and must test counter wrap, mode changes, compare matches, and interrupt routing independently. Host wall-clock timing is not an acceptable implicit guest-time source.
5. Record a new exact-main Windows/MSVC real-title probe before changing compatibility status. The current GPR observation is from the PR #82 head, not a fresh probe of merged main `431f71d`.

## Consequences

- The immediate M4 frontier is now correctly classified as an EE Timer 3 access in relocated code.
- The earlier partial `T3_MODE` implementation from ADR 0073 remains valid for its documented scope.
- No timer behavior, title compatibility, boot stage, or gameplay capability is added by this correction.
- Further work should inspect the timer's actual guest setup and consumption before implementing a shared EE timer subsystem.

## Validation

- Confirmed `main` at `431f71d8238ffcd16faaf64cec7ec162c8bb4fad`, with PRs #79–#82 merged and no open pull requests at audit time.
- GitHub Actions CI run `42978407701` for that main commit completed successfully, including Rust MSRV, Linux, Windows/MSVC, progress dashboard, and Windows Desktop Alpha jobs.
- Source comment/address constants and regression assertions clarify the mode-only boundary; no Timer 3 behavior is implemented. Local native build and real-title probe were not run in this environment; the PR CI is the code validation gate.
