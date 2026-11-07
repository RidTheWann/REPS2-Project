# ADR 0083: Native build identity and configuration-specific cache fingerprint

- Status: Accepted
- Date: 2026-11-07
- Related: ADR 0028 (deterministic frontend manifest), ADR 0029 (native build driver), ADR 0032 (native toolchain provenance).

## Context

ADR 0032 records the configured C++ toolchain but leaves a gap before REPS2 can safely key native artifacts. A compiler ID/version alone cannot distinguish changed compiler binaries, runtime headers, generated translation units, the boot ELF, the initial RAM image, or the runtime ABI and IR/manifest contracts. A cache must not reuse an artifact merely because the output filename is unchanged.

The generated frontend project knows the exact ELF bytes and emitted C++/RAM files. CMake, at configure/build time, can identify and hash the actual host compiler, runtime headers, and active configuration without placing machine-specific paths into portable metadata.

## Decision

Add an explicit versioned runtime ABI declaration in `runtime/include/reps2/runtime/abi_version.hpp`, included by `ee_state.hpp`. Add `CONTROL_IR_SCHEMA_VERSION` in the Rust control-IR module and combine it with `build_manifest::MANIFEST_VERSION`.

The generated CMake project extends its schema-version-1 toolchain report with:

- compiler identity, version, target/frontend/architecture, executable SHA-256, compiler argument and launcher metadata, and linker executable SHA-256 when CMake exposes one;
- CMake version, generator, host/target platform and active configuration;
- C++ standard, target compile options, global/per-configuration compile and linker flags;
- runtime ABI version and a deterministic digest of sorted relative public runtime-header paths plus their contents;
- manifest and control-IR schema versions;
- exact source boot-ELF SHA-256 when generated from a boot ELF (null for synthetic fixtures);
- deterministic digest of the ordered generated dispatcher/part source files; and
- initial RAM-image digest when supplied.

The complete configuration-specific toolchain report is hashed with SHA-256 when the generated native target is built. The companion `reps2-native-build-identity-<config>.json` contains the identity schema, kind, digest and corresponding report filename. Generator expressions keep Debug/Release and other multi-config builds separate.

Absolute compiler/linker/runtime include paths, source paths, timestamps, usernames and machine names are not recorded. Compiler executable content is represented only by its digest.

## Consequences and limitations

- The identity changes when any field in the toolchain report changes and can be used as a future native artifact cache key.
- This does not yet implement cache lookup, reuse, eviction, or stale-artifact rejection.
- The current direct ELF native-build path identifies the exact boot ELF, not the parent ISO. The eventual per-game cache must bind ELF identity to the content-addressed ISO/disc digest.
- Compiler wrappers, external tools not exposed through CMake, arbitrary process environment variables, and dependencies outside the public runtime headers are not fully fingerprinted.
- A matching identity is necessary but not sufficient proof that generated code is correct or that a PS2 title is playable.

## Validation

The native build driver smoke test checks the digest fields and schema versions, builds the generated project on Windows and Linux, verifies that the configuration-specific identity file exists, and independently recomputes the SHA-256 of the toolchain report to verify the recorded key.
