# ADR 0141: Serve CDVD SearchFile from the host ISO 9660 metadata

- **Status:** Proposed (validated locally; real-title probe recorded below).
- **Date:** 2026-11-15.
- **Scope:** CDVD HLE SearchFile handler plus a new bounded ISO 9660 reader
  (`runtime/include/reps2/runtime/iso9660.hpp`). Runtime ABI 8 -> 9 (six
  observation fields added to `EeSifCmdState`).

## Context

The SearchFile server (sid 0x80000597) has been registered since ADR 0125 but
its handler only recorded and refused calls: the ISO 9660 path walk it needs was
not implemented, so `searchfile_calls` stayed 0 in every probe. The observed
title has not yet issued a SearchFile call, so this change is driven by the
protocol evidence, not by a new stop.

Evidence:

- **Observed (pinned PS2SDK `ee/rpc/cdvd/src/libcdvd.c`):** `sceCdSearchFile`
  sends `SearchFilePkt` { 32-byte padding, 256-byte name, pointer to the
  caller's `sceCdlFILE` } (292 bytes without the layer field, 296 with it) and
  reads a 4-byte result: 1 when the file was found, 0 otherwise. The IOP also
  transfers the 32-byte `sceCdlFILE` into the caller's structure.
- **Observed (pinned PS2SDK `common/include/libcdvd-rpc.h` and
  `iop/cdvd/cdvdfsv/src/cdvdfsv.c`):** the `sz124` request places `m_eedest`
  after the 256-byte path (offset 288); the `sz128` form places it at 292. The
  search result word is the handler's return value; the structure transfer is
  unconditional.
- **Observed (ISO 9660 layout, cross-checked against the REPS2 frontend's
  independent reader `rust/frontend/src/iso9660.rs`):** the primary volume
  descriptor sits at LSN 16 with `CD001` and version 1; its root record length
  is at byte 156 and the record follows. A directory record stores the extent at
  byte 2 (little-endian), the data length at byte 10, the recording date at
  bytes 18..24 (year, month, day, hour, minute, second, GMT), the flags at byte
  25 (0x02 directory, 0x80 multi-extent), the name length at byte 32 and the
  name at byte 33.
- **Observed (pinned PS2SDK `iop/cdvd/cdvdman/src/cdvdman.c`):** the reference
  requires a leading `\` and compares the final component with
  `strncmp(..., 12)`, case-sensitively, including the `;1` version suffix; it
  caches a single 2048-byte directory sector.

## Decision

1. Add `runtime/include/reps2/runtime/iso9660.hpp`: a bounded, read-only walk
   that resolves an 8.3 disc path to extent LSN, byte size, name and recording
   date. It reads only through `HostMediaSource` and fails closed on an absent
   source, an unreadable sector, a malformed descriptor or record, a
   multi-extent entry, more than 8 path components, or a directory larger than
   64 sectors. It never invents an entry.
2. Serve `LF`-style function 0 of the SearchFile server: read the 256-byte name
   at request offset 32 and the caller's structure pointer at 288 (292-byte
   request) or 292 (296-byte request); write 1 or 0 to the caller's 4-byte
   result buffer; on success write the 32-byte `sceCdlFILE` (lsn, size, 16-byte
   name, 8-byte date). On not-found the result word is 0 and the structure is
   left untouched, so the caller's own error handling runs and nothing is
   fabricated.
3. Three deliberate, documented deviations from the reference are recorded:
   (a) the walk reads every sector of a directory extent instead of caching one
   sector, which resolves entries the reference's cache would miss; (b) the name
   comparison is case-insensitive with a trailing `;1` stripped from both sides,
   so a caller that omits the version suffix still resolves; (c) on a miss the
   result word is 0 and the caller's structure is left untouched, while the
   reference transfers the structure unconditionally — REPS2 does not fabricate
   an entry it could not resolve. All three are tolerance/fail-closed choices,
   not claims about the reference.
   The `sceCdlFILE.date` packing follows the reference exactly:
   `cdvdman.c` lines 2947-2954 compute `file_year = iso_year_byte + 1900` and
   store it as a 16-bit LE word at `date[6..7]`, with `date[5..1]` from the
   record's month/day/hour/min/sec; `date[0]` is left zero because the
   reference's flag byte is not established by the fetched lines. Multi-extent
   entries (record flags `0x80`) are refused rather than truncated to their first
   extent.
4. `EeSifCmdState` gains `searchfile_replies`, `last_searchfile_send_size`,
   `last_searchfile_lsn`, `last_searchfile_size`, `last_searchfile_found` and
   `last_searchfile_replied`, so `REPS2_RUNTIME_ABI_VERSION` moves 8 -> 9.

## Consequences

- A SearchFile call is answered from the owned ISO's own metadata; the probe
  will show the resolved LSN/size when the title first issues one. The walk
  covers ISO 9660 level 1 8.3 names only: Joliet/Rock Ridge names, multi-extent
  files and the layer field are not interpreted.

## Validation

- `reps2_hle_syscall_codegen_smoke` gains `test_iso9660_resolves_a_disc_path`,
  `test_searchfile_serves_a_file_from_host_media` and
  `test_searchfile_reports_a_missing_file` (147 cases) over a synthetic ISO
  image built by the test (PVD at LSN 16, root at LSN 20, `FILE.EXT;1` at LSN 21
  with 1234 bytes): the resolver returns the extent/size, the handler writes 1
  and the 32-byte structure, and a missing file answers 0 without touching the
  caller's structure.
- CMake Release build and `ctest -C Release` (41/41); Rust fmt/clippy/test on
  Windows x64/MSVC.
- Real-title probe rerun (same title/digests, runtime ABI 10,
  `PostADR140_142/run-report-abi10.json`): the title has not issued a SearchFile
  call yet (`searchfile_calls=0`), so this change is validated by the synthetic
  fixture tests above and will be exercised by the real title when it reaches a
  file lookup. Not a boot stage.
