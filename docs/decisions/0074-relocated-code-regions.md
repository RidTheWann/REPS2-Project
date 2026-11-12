# ADR 0074: Relocated code regions for runtime-copied guest code

- **Status:** Accepted
- **Date:** 2026-11-06
- **Scope:** Rust frontend analysis, M4 real-title execution

## Context

Downhill Domination's `InitAlarm` path (ADR 0073) copies a 0x740-byte blob from
the input image at `0x002829F8` to EE main RAM at `0x80076000`, then registers
that address as the handler for syscall 0x5B. The blob is the ps2sdk alarm patch
and is linked to run at `0x80076000`; its own `GetEntryAddress` implementation
searches a six-entry `(number, handler)` table at `0x80076710` and returns the
matching stub address.

The recompiler analyses the ELF's code sections. The blob is `.data` in the ELF,
so no blocks existed at `0x80076000` and the probe stopped with
`unresolved_control_flow` at exactly that PC. Executing the copy is not optional:
the guest registered the slot and the dispatcher correctly entered the guest
handler.

## Decision

Add an explicit frontend input:

```
recompile-elf <elf> <out.cpp> --relocated-code DEST:SOURCE:SIZE [--relocated-code ...]
```

- All three values are hexadecimal. `DEST` must be word-aligned; `SIZE` must be
  a nonzero multiple of four.
- `SIZE` bytes are read from the **initial EE main-RAM image** at the virtual
  address `SOURCE` (through the normal EE main-RAM alias translation) and are
  analysed as executable guest code at `DEST`.
- Each region becomes an additional `LoadedSegment` with read/write/execute
  flags and an authoritative entry point labelled `relocated`.
- The initial RAM image written to `<stem>.ram.bin` is **unchanged**. The guest's
  own `Copy` remains an observable guest action; the analysis input is only a
  description of where that copy will land.
- The build manifest (version 3) records every declared region as
  `{destination, source, size}` so a build is reproducible and auditable.

The compiler does not guess regions: it does not emulate the guest's `Copy`
calls or scan data for code. A title whose runtime-copied code is not declared
still stops at that address as `unresolved_control_flow`.

## Consequences

- Runtime-copied drivers can be translated without a runtime interpreter or a
  JIT. Only code reachable from the declared root is translated; other functions
  in the blob are added when an observed call reaches them.
- The invocation is per-title developer input, like the ISO and boot-ELF paths.
  It is not a compatibility claim and does not make the frontend title-specific.
- Code copied from a source outside EE main RAM (for example a BIOS region) is
  rejected with an explicit error.

## Verification

- `analysis::tests::relocated_region_becomes_an_executable_root` checks that a
  synthetic relocated segment becomes an authoritative root, decodes, and
  produces a block at its destination.
- `build_manifest` tests cover the version bump and deterministic rendering.
- Real-title evidence is recorded in `docs/PROJECT_STATE.md` and
  `docs/RESEARCH.md`.
