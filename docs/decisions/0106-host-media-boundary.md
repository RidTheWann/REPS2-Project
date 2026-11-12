# ADR 0106: Host media boundary for disc I/O

- **Status:** Accepted
- **Date:** 2026-11-09
- **Scope:** shared C++20 runtime (`runtime/include/reps2/runtime/host_media.hpp`, `ee_state.hpp`), new `reps2_host_media_smoke` test
- **Depends on:** ADR 0104 (bind reply and the retry loop the title waits in), ADR 0098 (SIFCMD device model), ADR 0001-class boundaries: the runtime never performs file I/O on the host's behalf

## Context

The title's next evidenced requirement is a libcdvd server that can answer real
disc reads (`docs/RESEARCH.md`). Any such server needs a media source at
runtime, and the runtime currently has none: the generated probe executes from a
RAM image and never touches a file. Introducing file access inside the runtime
would put host I/O, path handling and error policy into the device models, where
a mistake either breaks the "no fabricated device data" rule or leaks host state
into guest-visible behaviour.

This ADR fixes the boundary first, so later CDVD work has one narrow place to
read disc data from and no other place to get it.

## Evidence

- **Observed (ADR 0104 probe trace):** the title retries
  `sceSifBindRpc(cd, 0x80000592, 0)` and cannot advance on budget alone; its
  strings name libcdvd and an `IOPRP270.IMG` + IRX module list.
- **Observed (ps2sdk `ee/rpc/cdvd/*`, `iop/cdvd/*`):** libcdvd reads are served
  from 2048-byte user sectors addressed by absolute LSN, and `sceCdSearchFile`
  answers from the ISO9660 PVD at LSN 16 and the path tables.
- **Observed (existing frontend):** the Rust frontend already reads the user's
  ISO read-only for analysis and never modifies it; the runtime has no media
  access at all.
- **Derived:** disc data can only come from a host-supplied, read-only sector
  source. Anything else — an embedded image, a synthesized directory, a default
  sector — would be invented data.
- **Unknown:** whether this title reads through libcdvd directly or through the
  IOP `fileio`/`cdfs` path; either way the media source is the same.

## Decision

1. Add `HostMediaSource` (`host_media.hpp`): an optional host-installed,
   read-only sector source — an opaque `context` plus a
   `read_sectors(context, lbn, count, uint8_t* dest)` callback and a 2048-byte
   sector size. It is plain data; it owns nothing and performs no I/O itself. The
   host owns `context` and must keep it valid while an `EeState` can issue reads;
   a null context is allowed only for callbacks that do not require one.
2. `EeState::media` holds it as host-owned state, default-constructed to absent.
   The runtime never constructs one, never opens a file, and never derives disc
   contents from anywhere else. Reads accept a writable `std::span`; an
   undersized span is refused before the host callback, and the callback receives
   exactly `count * 2048` bytes even when the caller's span is larger.
3. `HostMediaSource::read` treats a zero-sector request as a no-op success, even
   when no source is installed. For non-empty reads it refuses an absent source,
   a null destination, or an LBN range crossing the 32-bit address space, using
   64-bit arithmetic. Each host adapter must additionally validate the actual
   image length before copying. The boundary prevents arithmetic wrap; the host
   adapter enforces the image-specific limit.
4. A device model that needs disc data must check `present()` and fail closed
   with an explicit diagnostic when the source is absent or refuses a read. It
   must not substitute zeros, a default sector, or an error code the protocol
   does not define.
5. Production behaviour is unchanged: no device model consumes the boundary yet,
   so the probe trace is identical while `media` is absent.

## Alternatives considered

- **Let the runtime open the ISO itself.** Rejected: it puts path handling,
  access policy and error reporting inside device models, and would let a
  binary behave differently depending on a path the guest must not influence.
- **Embed the disc image or its directories in the generated artifact.**
  Rejected: the artifact is a build product committed nowhere, and the set of
  files a title reads is not known before it runs; embedding also duplicates the
  user's licensed data inside REPS2's own output.
- **Serve a synthetic directory so binds and searches succeed.** Rejected
  outright: fabricated disc contents are exactly what the runtime must never
  produce.
- **Wait for the M7 library to define media access.** Rejected: the boundary is
  what the M7 library will install; defining it now keeps the CLI and the
  library on one contract.

## Consequences

- There is exactly one way disc data can enter the runtime, and it is
  host-controlled, read-only, and range-checked.
- Device models can be written against a stable, testable boundary while their
  protocol semantics are still being researched.
- Nothing guest-visible changes until a device model consumes the boundary, so
  the current M4 evidence stands.

## Validation

- New `reps2_host_media_smoke` covers: absent-source and empty-read behaviour,
  `EeState`'s default-absent source, exact forwarding of valid reads, rejection
  of undersized destination spans before callback invocation, propagation of
  source failure without destination mutation, and a range crossing the 32-bit
  LBN space being refused before the host source is called. The last
  representable sector is forwarded to the source for its image-length check.
- CMake Release build and `ctest -C Release` — 35/35.
- Real-title probe: unchanged (no device model consumes the boundary yet).

## Limitations and next evidence

- No device model consumes the boundary yet; no libcdvd server exists.
- The boundary exposes raw user sectors only. ISO9660 directory interpretation,
  the libcdvd command set and the SIFRPC server surface are separate units.
- Write paths (disc writes, memory cards) are out of scope; the boundary is
  read-only by construction.
- Disc-identity answers (disc ID, region, model, clock) depend on Mechacon/NVM
  state that this boundary cannot supply and must stay explicit failures.