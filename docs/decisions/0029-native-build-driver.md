# ADR 0029: Generated native build project and host probe

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** `native_build` rendering, `recompile-elf --native-build`, `compile-cfg-fixture --native-build`, and `reps2_native_build_driver_smoke`

## Context

ADR 0027 made large retail CFGs buildable as a dispatcher plus independently
compilable parts, and ADR 0028 made every `recompile-elf` invocation produce a
deterministic build manifest. Both left a gap between the emitted C++ and a
running native executable:

- The manifest records the translation layout, but it cannot record a host
  compiler it never invoked.
- A consumer still had to hand-write a CMake project, pick an include path for
  the header-only runtime, add the generated RAM image, and write a host
  `main` before it could observe a single generated block execute.
- Doing that differently per experiment made the frontend/native boundary hard
  to verify and easy to drift.

The frontend must not invoke or guess a C++ compiler. It can, however, emit a
complete, deterministic project that a host build system consumes. That keeps
the boundary honest: the frontend describes the build, the host toolchain owns
compiler selection and flags.

## Decision

### 1. `--native-build` writes `<stem>.native/`

`recompile-elf` and `compile-cfg-fixture` accept `--native-build`. When set, the
frontend writes two files beside the translation:

    <stem>.native/
      CMakeLists.txt
      probe_main.cpp

`CMakeLists.txt` compiles the dispatcher, every part file, and `probe_main.cpp`
into `reps2_probe`, adds the caller-supplied `REPS2_RUNTIME_INCLUDE` directory,
requires C++20, and registers `reps2_generated_probe` with CTest. `probe_main.cpp`
includes only the documented runtime header `reps2/runtime/ee_state.hpp` and
declares the two generated entry points.

The project references its sibling translation files as `../<name>` and sets the
test working directory to the translation directory. The translation and its
`<stem>.native/` directory are therefore a single movable unit.

### 2. Probe behavior

The probe is intentionally small and bounded:

1. Start from a zero-filled 32 MiB EE main RAM (`kEeMainRamSize`).
2. Optionally load a raw RAM image from `argv[1]`, or from the compiled-in
   `<stem>.ram.bin` default for `recompile-elf`. Synthetic fixtures pass
   `nullptr`, because they execute against zero-filled RAM.
3. Reject an image whose length is not exactly 32 MiB.
4. Call `initialize_ee_state(state, image)` and fail on any non-`ok` status.
5. Read the entry PC, call `execute_cfg(state, budget)`, and print
   `entry`, `status`, `pc` and the requested `budget`.

`argv[2]` overrides the instruction budget; the CTest default is `0`, so a
fixture returns immediately and cannot run unbounded. The probe performs one
call and exits; it is a boundary check, not a frontend trace or a playable-game
harness.

### 3. Determinism and safety

The renderer is a pure function of the project stem, the ordered source list and
the optional RAM file name. It embeds no absolute paths, timestamps, user or
machine identifiers, and iterates only caller-ordered sequences. Identical
arguments produce byte-identical `CMakeLists.txt` and `probe_main.cpp`.

CMake and C++ string contexts are escaped separately: backslashes become `/` in
CMake paths, and quotes, `$`, `;`, newlines, carriage returns and tabs are
handled in each language's literal syntax. The CMake project name is derived from
the output stem and sanitized to ASCII alphanumerics and underscores, with an
empty stem falling back to `reps2_generated`.

The generated project links no runtime library: the current runtime is
header-only. When the runtime grows compiled components, the renderer must add
an explicit link target rather than silently assuming one.

### 4. Scope

This increment generates and validates a project. It does **not**:

- invoke a compiler, configure a build, or write build outputs from the
  frontend;
- select or record a host compiler identity/options, or add them to the build
  manifest;
- turn a generated project into a registered title module or a game launcher;
- claim that a successful compile or probe run implies playability. The
  manifest still records `playable: false`.

Recording the host toolchain identity and keying the future build cache on it
remain separate roadmap items.

## Validation

Rust unit tests (`native_build`):

- `renders_ordered_sources_and_named_probe_test`
- `omits_ram_image_for_synthetic_fixtures`
- `sanitizes_project_names`
- `escapes_cmake_and_cpp_strings`
- `probe_exposes_the_runtime_boundary_and_bounded_execution`
- `rendering_is_deterministic`

CLI tests cover `--native-build` parsing for both commands. The full Rust suite
is 157 library + 5 binary tests; `cargo fmt --check` and
`cargo clippy -D warnings` are clean.

Native tests:

- `reps2_native_build_driver_smoke` (new) runs
  `compile-cfg-fixture --tu-count 4 --native-build`, asserts both generated
  files exist, configures the generated project with `REPS2_RUNTIME_INCLUDE`,
  builds it, and runs its CTest. CTest is 15/15 on Windows/MSVC.
- Manual `recompile-elf --native-build` on `Black_USA_iso.elf` (default single
  unit, no discovery) generated and built its project; the probe reported
  `entry=0x00100008 status=3 pc=0x00100008 budget=0`, matching the existing
  block-atomic `instruction_budget_exhausted` behavior.

This validates project generation, the compile/link path and the probe boundary.
It does not validate discovered-block semantics or title playability.

## Consequences

- The frontend/native boundary now includes a runnable host project, not just
  source and metadata.
- Reproducing a native probe no longer requires an undocumented, hand-written
  CMake setup.
- Generated projects remain build artifacts: they reference local translation
  paths and must not be committed with proprietary input paths or game data.
- The missing compiler-identity record is explicit and isolated; adding it does
  not change the rendered project contract.