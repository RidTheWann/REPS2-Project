# ADR 0128: Include a GPR snapshot in the engine run report

- **Status:** Accepted.
- **Date:** 2026-11-11.
- **Context:** The M4 frontier after ADR 0126/0127 is `unsupported_memory_access @ 0x800761A4` inside the relocated code. The faulting instruction is `lw $20, 0($2)`; without register visibility the faulting address could only be guessed. The engine run report carried PC, instruction count, COP0 frame and SIF observations but no GPRs.
- **Evidence:**
  - **Observed (Downhill Domination USA, branch on `d4496a7`, Windows x64/MSVC):** with the GPR snapshot added, the stop at `pc=0x800761A4` (executed=3,890,274) reports `$2 = 0xFFFFFFFFB0001800` — the KSEG1 alias of physical `0x10001800`. Its exact register identity was corrected by ADR 0129: EE Timer 3 Count (`T3_COUNT`), not a DMAC register. The failure is an unmodelled timer-counter read, not a RAM fault.
- **Decision:** The `rg.reps2.engine.run_report.v1` session object gains a `gprs` array of the 32 low 64-bit lanes, printed as `0x%016X` uppercase hex. It is diagnostic metadata only; guest architectural state is unchanged and the report remains a single JSON object on stdout.
- **Alternatives considered:** dump GPRs only on error — rejected: the report is written once at the end, and a bounded probe should always carry the register context for triage; printing HI/LO/COP0 adds little until a specific stop needs it.
- **Consequences:** Every future guest stop is now classifiable from the report alone (faulting address = base register + offset). No compatibility or boot claim is implied. The initial DMAC-area interpretation of `0x10001800` in this ADR's original evidence sentence is superseded by ADR 0129.
- **Validation:** CMake Release build and `ctest -C Release` 40/40 on Windows x64/MSVC; the report parses as valid JSON (verified with a Python `json.load` on the pr81b probe output).
