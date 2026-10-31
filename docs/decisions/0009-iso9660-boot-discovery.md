# ADR 0009: Read-only ISO 9660 boot executable discovery

- Status: Accepted
- Date: 2026-10-31
- Scope: Raw 2048-byte-sector ISO 9660 images, PVD namespace, SYSTEM.CNF BOOT2/BOOT target extraction

## Context

REPS2 previously required an ELF file to be extracted manually before it could be inspected. PS2 optical discs commonly identify the boot executable through SYSTEM.CNF. Disc images may be several gigabytes, so reading the entire image into a Vec is inappropriate. The parser must treat image metadata and directory records as untrusted input.

ECMA-119 defines the volume descriptor sequence and directory/file layout. PS2-specific public references document BOOT2 paths using the cdrom0 device prefix. The exact supported subset is intentionally smaller than the standard and actual console media support.

## Decision

- Use a generic Rust `Read + Seek` reader. Query the image length and read only the required descriptor sectors, directory extents, SYSTEM.CNF, and selected boot file.
- Accept only raw images whose byte length is a multiple of 2048 and whose logical block size is 2048. Reject unsupported raw-track/container formats with a diagnostic rather than guessing an LBA stride.
- Scan a bounded descriptor sequence beginning at logical sector 16. Validate the `CD001` identifier and version, find exactly one PVD and a descriptor-set terminator, and reject unknown/reserved descriptor types.
- Require volume set size and volume sequence number 1. Validate both-endian numeric fields, declared volume byte length, root directory extent, directory record extent, and actual image bounds.
- Parse PVD primary identifiers, directories, and single-extent files. Normalize ISO file versions and case for path lookup. Honor zero padding to the next logical block and reject records crossing a logical block boundary.
- Bound directory sizes to 16 MiB, directory entries to 100000, path depth to 32 components, SYSTEM.CNF to 64 KiB, and one extracted boot executable to 512 MiB.
- Reject parent-directory traversal, unsupported volume sequence numbers, multi-extent entries, interleaved entries, and file paths that name devices other than cdrom0/cdrom.
- Parse SYSTEM.CNF key/value lines case-insensitively. Prefer BOOT2; use BOOT only when BOOT2 is absent. Ignore arguments after the first whitespace-delimited executable path token.
- Validate the selected boot file through the existing ELF parser and require little-endian ELF32 EM_MIPS ET_EXEC before extraction.
- Open the ISO with read-only file access. Create the destination with exclusive-create semantics; never overwrite an existing file or mutate the source image.

## Non-goals

- 2352-byte raw BIN/CUE sector decoding, CHD, UDF, Joliet, Rock Ridge, multi-volume spanning, multi-extent concatenation, interleaved file sections, HDD/PFS loading, path-table acceleration, subchannel data, and CDVD command/timing emulation.
- Full PS2 ELF ABI interpretation, PT_LOAD placement, relocation application, complete R5900 instruction coverage, and game compatibility.

## Validation

Synthetic images cover PVD/descriptor handling, single-volume checks, both-endian field consistency, directory record bounds, root listing, version-normalized path lookup, SYSTEM.CNF BOOT2 preference and BOOT fallback, nested path normalization, parent traversal rejection, unsupported volume/descriptor types, multi-extent/interleaved files, and a synthetic boot ELF accepted by the existing ELF32 MIPS parser.
