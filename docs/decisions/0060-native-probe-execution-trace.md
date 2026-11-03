# ADR 0060: native probe execution trace and configurable budget

- **Status:** Accepted
- **Date:** 2026-11-04
- **Scope:** generated native build project (`--native-build`), host probe and the `execute_cfg` dispatcher

## Context

The generated `<stem>.native/` project compiles the dispatcher and registers a
CTest that runs the host probe. The probe loaded RAM, initialized `EeState`,
called `execute_cfg(state, budget)` once and printed only
`entry`, `status`, `pc` and `budget`. The generated CTest always passed budget
`0`, so `execute_cfg` returned `instruction_budget_exhausted` before executing a
single guest block. As a result the synthetic CTest proved that the project
built and linked, but never exercised translated guest code, and the trace
carried no exception or accounting detail.

The M4 milestone needs a repeatable, machine-readable observation of bounded
native execution on real translated code — not merely a successful build.

## Decision

1. Add host-only accounting to `EeState`:
   `std::uint64_t instructions_executed{}`. The generated dispatcher charges an
   admitted block's instruction count (the same `required` it subtracts from the
   budget) into this field. This is not EE-architectural state and is reset by
   `initialize_ee_state`. A block that faults part-way is charged in full,
   matching the existing block-atomic budget model.
2. Extend the probe output to one machine-readable line carrying `entry`,
   `status`, final `pc`, `budget`, `executed`, and the COP0 `cause`, `epc`,
   `cop0_status` and `badvaddr` registers.
3. Add `--probe-budget N` to `recompile-elf` and `compile-cfg-fixture`. The value
   is baked into the generated probe as its default budget and is therefore used
   by the generated CTest; `argv` still overrides it. The default remains `0`, so
   existing synthetic behavior is unchanged unless a budget is requested.
4. Give the generated CTest a `PASS_REGULAR_EXPRESSION` over the trace line so it
   fails if the trace fields disappear or the probe never reaches execution.

## Validation

- `native_build` unit tests assert the baked budget, every trace field and the
  CTest pass-expression.
- The native build driver smoke configures, builds and runs a generated project
  with `--probe-budget 64`, so the CTest executes translated blocks and must
  match the trace expression.
- The control-flow smoke fixture asserts the counter directly: a run rejected by
  the budget leaves `instructions_executed` at zero, and an admitted run charges
  at least the block that executed and never more than the budget.
- The remaining control-flow smoke fixtures continue to pass unchanged (their
  budget accounting is untouched).

## Consequences

The host probe now reports how much guest code ran and the COP0 state at the
stop, which supports the M4 bounded-execution evidence. The trace is an
observation surface only: the probe still returns 0 regardless of final status,
and no device, BIOS or interrupt behavior is added. A later milestone that
resumes after `exception_delivered` will extend, not replace, this trace.
