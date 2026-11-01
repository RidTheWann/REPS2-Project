# ADR 0032: Native host-toolchain provenance

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** generated native CMake project, CMake smoke test, build provenance report

## Context

ADR 0028 made the frontend manifest deterministic and deliberately independent of the
host C++ compiler. ADR 0029 made it possible to configure, compile, link, and execute
a bounded generated-code probe from a CMake project. A concrete reproducibility gap
remained: the generated project did not leave a machine-readable record of which
compiler, generator, configuration, or build flags were selected by the host.

The frontend must not invoke or guess a host compiler. Toolchain provenance therefore
belongs to the generated native project's configure/build directory, not to the
frontend's deterministic translation manifest.

## Decision

After the generated CMake project has identified its actual C++ compiler and
generator, configuration emits:

    <native-build-dir>/reps2-native-toolchain-<config>.json

The report uses schema version 1 and records:

- frontend version and report tool name;
- C++ compiler ID, version, target, frontend variant, and compiler architecture ID;
- CMake version, generator, generator platform/toolset, host platform, target platform,
  and active build configuration;
- required C++ standard and extension policy;
- the exact compile options applied by the generated target;
- global and per-configuration C++ compiler flags; and
- global and per-configuration executable linker flags.

Strings are JSON-escaped by the generated CMake project before serialization. The
report is written with file(GENERATE), so multi-configuration generators receive
a configuration-specific filename and contents. It intentionally omits absolute
compiler and runtime-include paths, timestamps, usernames, and machine-specific
source paths; the report is local build metadata, not a portable artifact.

The frontend manifest remains byte-deterministic and unchanged in meaning. This
separation preserves the rule that a frontend invocation cannot claim knowledge
of a compiler it did not execute. The generated CMake smoke test now requires the
report to exist after configure and validates its schema, compiler identity,
configuration, C++ standard, and target compile options before building and running
the probe.

## Scope and limitations

This report records CMake's configured compiler identity and flags; it does not
hash the compiler executable, capture arbitrary environment variables, or prove
that a compiler wrapper's implementation is immutable. It is therefore provenance
evidence, not yet a complete cache identity.

A future cache key must combine a stable host-toolchain fingerprint with at least
the input ISO digest, frontend/compiler version, IR and manifest schema, runtime
ABI, target platform, and relevant build configuration. Cache lookup and stale
artifact rejection remain separate work.

The report does not imply that a generated probe, a successful native build, or a
guest trap is evidence of retail-title boot or playability.

## Validation

- Rust tests assert that the generated CMake project declares the provenance
  schema and emits the configuration-specific report.
- tests/native_build_driver_smoke.cmake configures the generated project in Release
  mode on Windows and Linux, requires the JSON report, parses required fields with
  CMake's JSON support, then builds and runs the existing native probe through CTest.
- The existing frontend JSON manifest remains deterministic and timestamp-free.
