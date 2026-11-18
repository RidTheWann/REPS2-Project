# Frontier probes

`tools/frontier/frontier.py` turns one real-title investigation into a single
machine-readable record:

    recompile-iso -> native build -> engine run -> frontier.json

It orchestrates the existing entry points. It does not decode, translate or
emulate anything itself, and it adds no second build system.

## Why

The probe history used to live only in prose, so results were hard to compare and
easy to leave stale. A frontier record binds the disc identity, the frontend
revision, the runtime ABI, the build configuration and the run result into one
versioned JSON object that a person or an agent can diff.

## Usage

```powershell
python tools/frontier/frontier.py probe `
  --iso "D:\List Games\Downhill Domination (USA).iso" `
  --serial SCUS_971.77 `
  --expect-iso-sha256 0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86 `
  --out D:\REPS2-DevTests\DownhillDomination\frontier-01 `
  --reps2 D:\REPS2-Project\rust\target\release\reps2.exe `
  --engine D:\REPS2-Project\build\Release\rg-reps2-engine.exe `
  --runtime-include D:\REPS2-Project\runtime\include `
  --engine-include D:\REPS2-Project\engine\include `
  --reps2-commit 1cf3afdd8919a256b504c2efc5d290bafc644e20 `
  --discover --tu-count 8 --probe-budget 5000000 --max-batches 16 `
  --relocated-code 80076000:2829F8:740 `
  --relocated-code 80075000:2831A0:330 `
  --extra-root 0x00246CD0
```

`--out` must not exist. The tool never overwrites an existing probe directory.

Reproducibility: run the same command a second time into a new directory, then

```powershell
python tools/frontier/frontier.py compare frontier-01\frontier.json frontier-02\frontier.json
```

or pass `--compare-with frontier-01\frontier.json` to the second probe, which
sets `evidence.reproducible` and exits `7` when the semantics differ.

The comparison walks the **whole** record field by field and subtracts an explicit
volatile list, so a field added later is compared by default instead of being
silently ignored. It covers title identity, frontend revision, frontend and engine
executable digests, runtime ABI, the toolchain header digest, the normalized
manifest digest, the native module and RAM image digests, the host toolchain, the
configuration, the whole `result` block and the manifest census.

Three fields are excluded, and only these:

| Excluded | Why |
| --- | --- |
| `evidence.report_sha256` | hashes the run report of that specific execution, which carries elapsed time and host paths |
| `source.manifest_sha256` | raw digest of a manifest that embeds the absolute `boot.elf` path, so it varies with the probe directory name; `source.manifest_normalized_sha256` is the comparable identity |
| `evidence.reproducible` | the result of the comparison itself |

Both records must pass `validate` before they are compared; a record that fails
schema validation makes `compare` and `--compare-with` exit `6` instead of
producing a verdict. A pairwise walk cannot see a field missing from *both*
records, so validation is not optional. A key present on one side only is itself
reported as a difference.

Validate any record:

```powershell
python tools/frontier/frontier.py validate frontier-01\frontier.json
```

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | probe succeeded and the record validates |
| 2 | invalid input: missing ISO, ISO digest mismatch, existing output directory |
| 3 | `recompile-iso` failed |
| 4 | native configure/build failed, or the manifest kept an absolute path after normalization |
| 5 | engine runner failed or produced an unexpected report |
| 6 | a produced record, or a record passed to a comparison, failed schema validation |
| 7 | a comparison found a semantic difference |
| 8 | provenance check failed: the frontend executable does not report the version its manifest claims, or the engine runner does not report the version its own run report claims |

A non-zero code never means "the guest did something"; code 0 never means "the
title boots". Code 0 means a bounded host run completed and the evidence record is
internally consistent.

## Record and schema

`schema_version` is `2`; `kind` is `reps2.frontier.v2`. The declarative schema is
`tools/frontier/frontier.schema.json`; the executable checker is
`frontier.py validate`, and a test keeps the two in agreement. A version-1 record
was produced by the pre-comparison-fix tool and is rejected rather than compared,
because the v2 comparison and its `evidence.reproducible` flag mean something
stricter.

| Section | Content |
| --- | --- |
| `title` | disc serial, ISO SHA-256, extracted boot ELF SHA-256 |
| `source` | REPS2 commit, frontend version and its reported version line, frontend executable digest, engine executable digest, runtime ABI, runtime header digest, raw and normalized manifest digest, native module digest, RAM image digest |
| `host` | OS, architecture, compiler id and version, C++ standard |
| `configuration` | discovery, translation-unit count, HLE flag, instruction budget, batch limit, extra roots, relocated regions |
| `result` | outcome, guest status and code, stop reason, final PC, instructions, batches, syscall and RPC counters |
| `evidence` | run report digest, manifest census, reproducibility flag |

### Provenance of the revision claim

`source.reps2_commit` is asserted on the command line and cannot be read out of a
binary: neither the frontend nor the engine embeds a git hash, and `reps2 version`
reports only `0.7.0`. The record therefore binds the claim to the exact binaries
that ran:

- `source.frontend_sha256` and `source.engine_sha256` are the digests of the
  `--reps2` and `--engine` files that were invoked.
- `source.frontend_reported_version` is the first line of `reps2 version` from that
  frontend, and the probe fails closed (`8`) when its version differs from
  `source.reps2_version`.
- The probe also queries the engine runner's own `--version` and fails closed (`8`)
  when it disagrees with the version in the run report the engine just produced.

So a record supports "these two binaries, reporting these versions, produced this
result". It does not prove that the binaries were built from `reps2_commit`; it
makes the claim checkable against a named digest pair.

### Known build nondeterminism (observed 2026-11-18)

Two consecutive ABI-19 probes of the same configuration (Downhill Domination USA,
`reps2_commit` `1cf3afd`, runtime ABI 19, identical command) produced an identical
`result` block (`4,586,235` instructions, `semaphore_wait_blocked` at
`0x0025A724`), an identical `source.ram_image_sha256` and identical runtime header
digests. The comparison reported exactly one difference:

```text
record.source.native_module_sha256: 35086e49... != 7db941db...
```

The two module files differ in four bytes out of 89,748,480: two copies of the link
timestamp (the COFF `TimeDateStamp` at file offset `0x110`, and its copy in the
debug directory at `0x51B4CE4`). No code byte and no path string differs. The
generated native project passes no deterministic-link option —
`rust/frontend/src/native_build.rs` emits no `/Brepro` — so the module link is
currently not reproducible.

Consequence: `evidence.reproducible` is `false` for two otherwise identical runs
until that link is made deterministic. This is the flag working as intended: it
reports a real difference in the recorded artifact. It is **not** evidence that the
guest behaviour differed, and it is not a reason to stop comparing
`source.native_module_sha256`. Passing `/Brepro` to the module link is a separate
change that makes the flag reachable.

### Translation units

`--tu-count 8` is the number of *part* translation units requested.
`evidence.manifest_census.translation_units` counts the generated translation
units as a whole, which is those part files plus the `boot.cpp` dispatcher, so the
baseline Downhill configuration records `tu_count=8` and `translation_units=9`.
They are different quantities, not a disagreement; a record whose
`translation_units` is not `tu_count + 1` is worth a look, not automatic proof of a
defect.

`--jobs N` forwards `--parallel N` to the native `cmake --build`. Without it the
build is serial, and one part translation unit of a large title can take close to
an hour. It changes only build wall time: the generated sources, the manifest and
the recorded result are unaffected, so two runs that differ only in `--jobs` are
still comparable.

`source.toolchain_headers_sha256` is recorded instead of the engine's
`kToolchainAbiFingerprint` because the fingerprint is a compile-time constant that
the current run report does not expose. The module must still pass the engine's
ABI and fingerprint admission checks to run at all; the header digest binds the
layout inputs that produce the fingerprint. Adding the fingerprint to the run
report is a separate reporting change, not a silent substitute.

## Sanitization

Records contain no disc path, no user directory, no ISO/ELF bytes, no BIOS or IOP
image content, no generated module and no personal data. The validator rejects any
string that looks like an absolute host path, and the manifest normalizer refuses to
reduce a manifest that carries an absolute path it does not explicitly canonicalize,
so a path cannot reappear in the digest unnoticed. Raw reports and generated modules
stay in the operator's own probe directory and are never committed.

## Limits

- CI runs only the synthetic schema and CLI tests. Real-title probes need the
  owner's local ISOs and stay local; the committed record is the sanitized result.
- One record describes one configuration. A different budget, batch limit, root
  set or relocated region is a different frontier.
- `evidence.reproducible` is only about fields *compared*. A comparison that passes
  shows the two runs agree on every compared field; it does not show that the build
  is bit-reproducible beyond those digests, and it never establishes a boot stage.
- A frontier record is evidence for `Native-built` at most. `Boot-tested` still
  requires a guest-visible stage, and `Playable` requires a gameplay plan.