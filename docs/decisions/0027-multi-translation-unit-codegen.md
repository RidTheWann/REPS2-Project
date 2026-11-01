# ADR 0027: Per-block functions and multi-translation-unit CFG codegen

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** `control_codegen` emission shape, `recompile-elf --tu-count`, and the split-codegen native smoke test

## Context

ADR 0026 made stripped retail executables analyzable. On `Black (USA)` the
discovered CFG is 97,678 blocks / 473,292 decoded instructions, and
`recompile-elf --discover` produced a single C++ translation unit of roughly
178 MB. A single file of that size has two concrete problems:

1. It cannot be compiled in parallel, and its compile time and peak memory are
   dominated by one translation unit.
2. The previous emitter lowered every block into one `execute_cfg` function with
   a `switch (state.pc)` containing one `case` per block. A 97,678-case switch is
   a poor code-shape target for an optimizing compiler, and it keeps every block
   in one function.

The emitted code also imported the runtime helpers inside `execute_cfg` and
included only `<bit>`/`<cstdint>`. That worked for the synthetic fixture through
argument-dependent lookup but was fragile for real programs: helpers that take
scalar arguments (`is_word_value`, `sign_extend_*`, `apply_fpu_*`) are not found
by ADL, and emitted DIV uses `std::numeric_limits` without `<limits>`.

## Decision

### 1. One function per basic block

Each block is emitted as
`reps2::generated::block_0xXXXXXXXX(EeState& state, std::uint64_t& remaining) noexcept`.
The function checks and debits the guest-instruction budget first, then executes
the block body and its terminator. Normal control transfers end with
`return Status::ok;`; unsupported and segment-end terminators return their
existing statuses. This preserves the previous behavior exactly:

- the budget is still checked and debited atomically per block;
- `state.pc` is still left at the block start when the budget is exhausted;
- early status returns from memory or scalar checks still propagate unchanged.

### 2. Sorted table dispatch

`execute_cfg` looks the block up in a `constexpr BlockEntry kBlocks[]` sorted by
guest PC using `std::lower_bound`, then calls the matching function. A PC that is
not a block start still returns `Status::external_control_flow`, matching the old
`default:` arm. Dispatch is therefore O(log n) and needs no giant switch.

### 3. Multi-translation-unit emission

`emit_cpp_units(program, tu_count)` returns the units a caller should write:

- `tu_count == 1`: one self-contained unit (dispatcher, table and all block
  definitions), preserving the original `emit_cpp` contract;
- `tu_count > 1`: units[0] is the dispatcher (block declarations, table and
  `execute_cfg`) and units[1..] contain block definitions. The dispatcher links
  against the parts through the declared block symbols.

Blocks are partitioned into contiguous, non-empty slices of ascending guest PC,
so output is deterministic. `tu_count` must be in `1..=block count`; anything
else is `ControlError::InvalidTranslationUnitCount`.

`emit_cpp_units_with_entry` appends the ELF entry-point initializer to units[0].
The existing `emit_cpp` / `emit_cpp_with_entry` APIs are thin wrappers over the
single-unit path.

### 4. CLI and file layout

`recompile-elf` accepts `--tu-count N` (default 1). When N > 1 the requested
output receives the dispatcher and each part is written beside it as
`<stem>.partNNNN.<ext>` (for example `Black.part0001.cpp`). Before writing, any
pre-existing `<stem>.partNNNN.<ext>` files are removed so a smaller follow-up run
cannot leave stale units behind. `compile-cfg-fixture` accepts the same flag so
CTest can build and run a split fixture.

### 5. Stable runtime surface

The shared prelude imports the full runtime helper list once at namespace scope
and includes `<algorithm>`, `<bit>`, `<cstdint>`, `<iterator>`, `<limits>` and
`<span>`. Every part is independently compilable and uses only the documented
runtime header.

## Measured result

`Black (USA)`, `recompile-elf --discover --tu-count 8` (97,678 blocks):

| unit | size |
| --- | ---: |
| dispatcher (`Black.cpp`) | 15.84 MiB |
| `part0001` | 19.78 MiB |
| `part0002` | 17.89 MiB |
| `part0003` | 23.87 MiB |
| `part0004` | 21.31 MiB |
| `part0005` | 20.08 MiB |
| `part0006` | 21.04 MiB |
| `part0007` | 21.67 MiB |
| `part0008` | 19.34 MiB |

Generation takes about 7 seconds. Compiling the dispatcher with MSVC 19.51
`/std:c++20 /O2 /W4 /permissive-` takes about 5 seconds; a representative
20 MiB part takes about 17 seconds. The dispatcher plus all eight parts compile,
link and run in a probe executable in **148.9 s sequentially with zero warnings**.
The probe calls the generated initializer with a 32 MiB RAM image, then calls
`execute_cfg(state, 0)`: it returns `instruction_budget_exhausted` and leaves
`state.pc` at the entry block start (`0x00100008`), confirming table dispatch and
block-atomic budget behavior across the split units. Parallelizing part
compilation reduces wall time to roughly the slowest part; the exact figure is
compiler- and machine-dependent and is not claimed here.

This validates the emission/link path, not the semantic correctness of discovered
blocks or playability of the title.

## Consequences

- Large retail CFGs can now be emitted as independently compilable units and
  linked with `execute_cfg` in the dispatcher.
- Block dispatch is logarithmic instead of a compiled switch, and each block is a
  separate function that the compiler can optimize and the linker can place
  independently.
- Part count is explicit rather than automatic. Choosing a good `--tu-count` is
  currently the caller's responsibility; a size-targeted policy can be added
  later without changing the unit contract.
- `tu_count` is capped by the block count, so tiny synthetic programs cannot ask
  for empty parts.
- Generated files remain build artifacts. No ISO, ELF, BIOS or emitted retail
  code is committed.

## Validation

Rust unit tests:

- `single_unit_emits_per_block_functions_and_lower_bound_dispatch`
- `partitions_blocks_across_requested_translation_units`
- `rejects_out_of_range_translation_unit_counts`
- `attaches_entry_initializer_only_to_the_dispatcher`

The existing branch/link/delay-slot ordering tests continue to pass, and the
full Rust suite is 144 tests. `cargo fmt --check` and
`cargo clippy -D warnings` are clean.

Native tests:

- `reps2_cfg_codegen_smoke`: the single-unit fixture, unchanged behavior.
- `reps2_cfg_split_codegen_smoke` (new): the same fixture emitted as one
  dispatcher plus four part files, compiled and linked by the host C++20
  compiler, and run against `tests/control_flow_smoke.cpp`. CTest is 14/14.