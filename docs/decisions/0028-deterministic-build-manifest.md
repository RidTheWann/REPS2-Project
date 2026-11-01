# ADR 0028: Deterministic build manifest for `recompile-elf`

- **Status:** Accepted
- **Date:** 2026-11-01
- **Scope:** `digest`/`json` extraction, the `build_manifest` schema, and the `recompile-elf` artifact set

## Context

`docs/ARCHITECTURE.md` names generated C++ plus a machine-readable build
manifest as the primary frontend/native boundary. Only the per-game library
import wrote a manifest (`game.json`); a direct `recompile-elf` invocation
produced C++ and a RAM image with no record of what was compiled, under which
limits, or how the translation was laid out. That makes builds hard to audit,
cache, or compare across machines and runs.

Three properties matter for the boundary:

1. **Traceability.** A consumer must be able to identify the exact input and
   analysis configuration that produced a translation.
2. **Determinism.** Re-running the same command with the same input must produce
   identical metadata. Timestamps, machine names and hash-map ordering would
   break that.
3. **Honesty.** Analysis success and generated C++ are not evidence of
   playability, so the manifest must never imply it.

The SHA-256 implementation already existed privately inside `library.rs`, and
JSON string escaping was duplicated there. Adding a second writer without
sharing those routines would invite drift between the library schema and the
build schema.

## Decision

### 1. Shared `digest` and `json` modules

`Sha256`, `sha256_bytes`, `sha256_reader` and `hex_digest` move from
`library.rs` into `digest.rs`, together with their known-answer and
chunk-boundary tests. `json_string` and `json_optional_string` move into
`json.rs`. Both modules are public library modules; the library importer and the
build manifest use the same implementations.

### 2. Schema version 1

`build_manifest.rs` defines `MANIFEST_VERSION: u32 = 1` and renders fixed-order
JSON with these sections:

- `tool`: frontend name, crate version, and exact host target triple.
- `input`: path as supplied, size in bytes, and lowercase SHA-256.
- `guest`: ELF entry PC, `e_machine`, `e_type`, and loadable segment count.
- `analysis.limits`: every bound in `AnalysisLimits`.
- `analysis.discovery`: `null` when discovery is off; otherwise candidate,
  call-target, prologue, scanned-word, truncation, accepted-root, skipped-root
  and skipped-block totals.
- `program`: entry points, basic blocks, guest instructions, explicit
  unsupported stops, static edges, unresolved indirect targets, and
  external/unmapped targets.
- `translation`: total unit count, primary output file name, and the ordered
  part file names.
- `artifacts`: initial RAM file name and byte length.
- `playable`: always `false`.

Only counts are recorded for discovery and control-flow sets. Full
`skipped_blocks`, unresolved-target and external-target address lists already
exist in analysis and can be added in a later schema version without
retroactively changing version 1.

### 3. Exact host target

`build.rs` emits `REPS2_BUILD_TARGET` from Cargo's `TARGET` variable, and
`lib.rs` exposes it as `BUILD_TARGET`. Recording the target triple is more
precise than inferring it from `std::env::consts` and costs one build-script line.

### 4. CLI artifact

`recompile-elf` writes `<stem>.manifest.json` beside the requested output. For
`game.cpp` the manifest is `game.manifest.json`; the RAM artifact remains
`game.ram.bin` and parts remain `game.partNNNN.cpp`. The command prints the
input digest and manifest path.

### 5. Determinism

The renderer iterates only fixed field sequences and the caller-supplied part
list. It writes no timestamps, temp paths, user names, machine identifiers, or
unordered-collection iteration. Identical arguments and input therefore yield a
byte-identical manifest. This is enforced by a unit test and by hashing the
manifest across two real invocations.

### 6. Scope of `playable` and compiler options

`playable` is hard-coded to `false` in this schema rather than being a caller
field. The frontend does not invoke, identify, or guess a host C++ compiler, so
compiler identity and flags are deliberately absent; they belong to the future
native build driver, which can write its own record once it exists. The ROADMAP
tracks that gap explicitly.

## Validation

Rust unit tests:

- `renders_deterministically`
- `records_versioned_identity_and_non_playable_status`
- `renders_discovery_statistics_when_present`
- `escapes_paths_in_json_strings`
- `renders_part_outputs_in_order`
- `sha256_reader_matches_in_memory_hashing`
- `json_strings_escape_control_characters_and_quotes`

The full Rust suite is 155 tests (151 library + 4 binary);
`cargo fmt --check` and `cargo clippy -D warnings` are clean.

Manual verification against `build/isoaudit/Black_USA_iso.elf`:

- Default (no discovery): manifest records 1 unit, 9 blocks, 113 instructions,
  `discovery: null`, and the input SHA-256
  `141b05209570458af9c8e27ab1911f258da6353909d813cd572df026254ffe4c`.
- `--discover --tu-count 8`: manifest records 10,457 candidates, 10,394
  accepted roots, 97,678 blocks, 472,072 represented instructions, a 1
  dispatcher + 8 part layout, and `playable: false`.
- Running the default command twice produced the same manifest SHA-256
  (`D10FF8BAB3DCE56C70E45D7ABC7EEF66219E23A66F1CC420F6656FFC2E776391`).

The existing CMake/CTest suite remains 14/14; no native fixture consumes the
manifest yet.

## Consequences

- Every direct compilation now carries a reproducible record of its input,
  limits, program shape, and artifact layout.
- The boundary documented in `ARCHITECTURE.md` now has both halves in practice.
- A future build cache can key on the manifest's input digest, tool version,
  host target and limits without adding new metadata.
- Adding richer per-address diagnostics or host compiler options requires a
  schema version bump.
- Generated manifests contain local input paths. They are build artifacts and
  must not be committed with proprietary paths or game data.