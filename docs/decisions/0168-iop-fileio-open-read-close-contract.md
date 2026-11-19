# ADR 0168: Model IOP FILEIO disc and virtual file operations (fnos 0, 1, 2, 3)

- **Status:** Accepted.
- **Date:** 2026-11-19.
- **Scope:** `runtime/include/reps2/runtime/ee_hle.hpp`, `runtime/include/reps2/runtime/ee_state.hpp`, `docs/RESEARCH.md`.
- **Supersedes:** ADR 0167's fail-closed decision for function numbers 0 (open), 1 (close), 2 (read), and 3 (lseek) for ISO-9660 disc files (`cdrom0:...`) and virtual `rom0:ROMVER`. All other file paths and function numbers remain unmodelled and fail-closed.

## Context

Following ADR 0167 (which served the FILEIO `fno 255` handshake), Downhill Domination USA advanced from `WaitSema(11)` to `WaitSema(12)` at `pc=0x0025A724` (`$ra=0x0025D138`). Direct memory inspection of the request payload at `0x00763580` revealed that the title calls `sceOpen("cdrom0:\\SKAT\\DHSKAT.SKX;1", SCE_RDONLY)` via FILEIO `fno 0` (`send_size=1048, recv_size=4`).

Because `fno 0` was unmodelled, the runtime returned `false` without delivering an `RPC_END` reply, leaving the caller parked indefinitely on semaphore 12.

This ADR establishes the request/reply contracts and HLE virtual file descriptor implementation for the FILEIO file operations: `sceOpen` (`fno 0`), `sceRead` (`fno 2`), `sceLseek` (`fno 3`), and `sceClose` (`fno 1`), serving both ISO-9660 disc files via `state.media` and the virtual `rom0:ROMVER`.

## Observed

### 1. Downhill Domination Frontier Trace (Run 05 / 06)

- **Stop:** `pc=0x0025A724` (`WaitSema`, `$v1=0x44`, `$a0=12`, `$ra=0x0025D138`).
- **Instruction count:** 4,587,011 in 1 batch.
- **Issued RPC call:** `sid=0x80000001` (FILEIO), `fno=0`, `send_size=1048`, `recv_size=4`, `send_addr=0x00763580`, `recv_addr=0x007641C0`.
- **Request payload layout at `0x00763580` (Sony `libfileio` variant):**
  - Offset `0x00`: `0x0000000C` (semaphore ID 12).
  - Offset `0x0C` (+12): `0x00000001` (`flags = SCE_RDONLY`).
  - Offset `0x10` (+16): `0x00000000` (`mode`).
  - Offset `0x14` (+20): ASCII string `"cdrom0:\SKAT\DHSKAT.SKX;1"` (NUL-terminated).
- **Caller call stack:**
  - `0x0025D128`: `jal 0x0025A720` (`WaitSema(12)`).
  - `0x0025D100..0x0025D124`: `sceOpen` wrapper sets up request, calls `sceSifCallRpc(fno=0)`, then waits on semaphore.
  - Calling function: asset loading initialization (`SKAT` engine package loader).

### 2. Retail IOP FILEIO Handler Disassembly (`FILEIO.IRX`)

Extracted from `Downhill Domination (USA).iso` (`IOPRP270.IMG`):
- `fno 0` (`open`): extracts filename and open flags from request buffer; allocates IOP file descriptor; writes `fd` (or negative error code) to 4-byte reply buffer.
- `fno 1` (`close`): reads `fd` from offset 12; calls internal close; writes 0 (or negative error code) to 4-byte reply buffer.
- `fno 2` (`read`): reads `fd` from offset 12, destination pointer from offset 16, size from offset 20; transfers data to EE memory via SIF DMA; writes bytes read to 4-byte reply buffer.
- `fno 3` (`lseek`): reads `fd` from offset 12, offset from offset 16, whence from offset 20; updates read pointer; writes new file position (or negative error code) to 4-byte reply buffer.

### 3. PS2 ROMVER Specification

Standard PS2 ROM version string format (14 bytes ASCII, no newline):
`VVVVRTYYYYMMDD`
- `VVVV`: Version (e.g., `0160` for v1.60).
- `R`: Region (`A` = USA/America, `J` = Japan, `E` = Europe, `H` = Asia, `C` = China).
- `T`: Console type (`C` = Consumer, `T` = Tool/Development).
- `YYYYMMDD`: Release date (e.g., `20030110`).

For a US consumer console matching Downhill Domination USA's target: `"0160AC20030110"`.

## Derived

1. **Sony `libfileio` request packet structure:**
   - The open packet is 1048 bytes: 4 bytes sema ID, 8 bytes padding, 4 bytes flags, 4 bytes mode, 1028 bytes path buffer.
   - The filename is located at offset 20 (`0x14`).
   - The reply is a single 32-bit signed word representing the assigned file descriptor (>= 0 on success, < 0 on error).
2. **Read request packet structure:**
   - Offset 12: `fd`.
   - Offset 16: destination EE address (`ptr`).
   - Offset 20: byte count (`size`).
   - Reply is a 32-bit word representing bytes transferred.
3. **Lseek request packet structure:**
   - Offset 12: `fd`.
   - Offset 16: seek `offset`.
   - Offset 20: `whence` (0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END).
   - Reply is a 32-bit word representing the new absolute file position.
4. **Close request packet structure:**
   - Offset 12: `fd`.
   - Reply is 0 on success, -1 on invalid fd.
5. **Scope boundary:**
   - Disc files prefixed with `cdrom0:` (or bare relative paths) are resolved against the ISO-9660 filesystem in `state.media` via `iso_resolve_path`.
   - `rom0:ROMVER` is modelled as a virtual read-only file.
   - Any other path requested through `fno 0` that cannot be resolved is refused (returns `false`, IOP sends no `RPC_END`, caller remains blocked).

## Decision

1. Add a bounded HLE virtual file descriptor table (`fileio_fds[4]`) to `EeSifCmdState` tracking:
   - `in_use`: slot allocation flag.
   - `read_offset`: current position in the file.
   - `lsn`: LSN on disc (0 for `rom0:` files).
   - `file_size`: file size in bytes.
   - `file_id`: identifier of the file type (0 = `rom0:ROMVER`, 1 = ISO-9660 disc file).
2. Implement `fno 0` (`sceOpen`):
   - Parse filename at offset 20 of the request payload.
   - If filename matches `"rom0:ROMVER"`, allocate a free slot in the FD table initialized with `kEeFileioFileIdRomVer` and size 14.
   - Otherwise, resolve path via `iso_resolve_path(state.media, name, iso_entry)`. If found, allocate an FD slot with `lsn = iso_entry.lsn` and `file_size = iso_entry.size`.
   - Return slot index as `fd` in the 4-byte reply buffer.
   - If no free slots exist, return `-1`.
   - If path is not found, return `false` (fail-closed).
3. Implement `fno 2` (`sceRead`):
   - Read `fd`, destination `ptr`, and `size` from request offsets 12, 16, 20.
   - If `fd` is invalid or not in use, return `-1`.
   - For `rom0:ROMVER`, transfer available bytes from `kEeRomVerContent` starting at `read_offset` into EE memory at `ptr`.
   - For ISO-9660 files, transfer sectors from `state.media` across chunk boundaries into EE memory at `ptr`.
   - Advance `read_offset` by the number of bytes transferred.
   - Return actual bytes read (0 if already at EOF) in the 4-byte reply buffer.
4. Implement `fno 3` (`sceLseek`):
   - Read `fd`, `offset`, and `whence` from request offsets 12, 16, 20.
   - Calculate new position based on `whence` (SEEK_SET, SEEK_CUR, SEEK_END). Clamp to file bounds and update `read_offset`.
   - Return new offset in the 4-byte reply buffer.
5. Implement `fno 1` (`sceClose`):
   - Read `fd` from request offset 12.
   - If `fd` is valid and in use, mark slot free and return `0`.
   - If `fd` is invalid, return `-1`.
6. Keep diagnostic fields in `EeSifCmdState`: `fileio_open_count`, `fileio_read_count`, `fileio_close_count`, `fileio_last_fd`, `fileio_last_open_replied`, `fileio_last_read_replied`, `fileio_last_close_replied`.

## Consequences

- The `SKAT` engine asset loader in Downhill Domination USA successfully opens, reads, and closes files on the disc (`cdrom0:\SKAT\DHSKAT.SKX;1`).
- Execution advances past `WaitSema(12)` into subsequent boot stages.
- No other filesystem operations are affected; unknown files remain strictly fail-closed.
