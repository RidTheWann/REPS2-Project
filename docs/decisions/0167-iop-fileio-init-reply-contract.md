# ADR 0167: Serve the IOP FILEIO initialisation handshake (fno 255)

- **Status:** Accepted.
- **Date:** 2026-11-19.
- **Scope:** `runtime/include/reps2/runtime/ee_hle.hpp`, `docs/RESEARCH.md`.
  `EeState` and `EeSifCmdState` struct layouts are unchanged (ABI 19 preserved).
- **Supersedes:** ADR 0163's fail-closed decision for function 255 only. Individual
  I/O functions (fnos 0..254) remain unmodelled and fail-closed.

## Context

In ADR 0163 and ADR 0165, the Downhill Domination frontier stopped at
`WaitSema(11)` (`pc=0x0025A724`) during `sceFileioInit`. The guest issues an
RPC call to the FILEIO server (`sid=0x80000001`) with function number 255 and an
8-byte request carrying two work buffer pointers. ADR 0163 refused the call
because the reply structure was unevidenced. ADR 0165 corrected the static
dataflow, showing that reply word 0 is stored to `0x00764CA8` and reply word 1 is
tested against `2` (`0x0025D968`).

This ADR establishes the exact reply structure and semantics through direct
reverse-engineering of retail IOP binaries (`FILEIO.IRX`) and EE client code.

## Observed

### 1. IOP FILEIO Module Disassembly (`IOPRP270.IMG`)

Extracted from `Downhill Domination (USA).iso` (LBA 3598, size 249121 bytes,
SHA-256 `05c0b8c068903033dd85a383021f8d9a9630e685965b1497b2194c4c53110f63`):
`FILEIO.IRX` at offset 97120, size 17829 bytes, SHA-256
`852343bd504eca5b99148fdc44f23ab35545443cab1953f636853655f03239e7`.

- **RPC Server Registration (`0x1FF0..0x2018`):** Calls `sceSifRegisterRpc`
  passing `sid = 0x80000001`, handler function `0x1AA4`, receive buffer
  `0x2EB0`, and queue `0x2E50`.
- **SIFRPC Handler (`0x1AA4`):** Signature `void* handler(int fno, void *buf, int size)`.
  - `0x1ACC`: `addiu $v0, $zero, 255`
  - `0x1AE4`: `bne $s5, $v0, 0x1B58` (branches if `fno != 255`)
  - `0x1AEC`: `addiu $v0, $zero, 8`
  - `0x1AF0`: `bne $s6, $v0, 0x1B1C` (checks `size == 8`)
  - `0x1AF8..0x1B10`: reads `buf[0]` and `buf[1]` and stores to `.sbss` `[0x2E00]`
    and `[0x2E04]`.
  - `0x1B38`: builds pointer `$v0 = 0x2E40` (the 8-byte reply buffer in `.bss`).
  - `0x1B44`: loads `$a0` from `.data` `0x2D5C` (value `0x30303732`, ASCII `"2700"`).
  - `0x1B48`: sets `$v1 = 2`.
  - `0x1B4C`: stores `$v1` (2) to `reply[1]` (`0x2E44`).
  - `0x1B54`: stores `$a0` (`0x30303732`) to `reply[0]` (`0x2E40`).
  - `0x1B50`: jumps to epilogue `0x1DEC`, returning `$v0 = 0x2E40`.

### 2. IOP FILEIO Module Disassembly (`IOPRP300.IMG`)

Extracted from `Black (USA).iso` (LBA 1900185, size 275345 bytes,
SHA-256 `02d314464c17c715d7e809297b0aba184173a98616b1fafeac27fe33fc480c82`):
`FILEIO.IRX` size 19661 bytes, SHA-256
`270c6408794d7817c5e016dc7f0fba6df4e93db6cb31058dec480fd8270cdb42`.

- **SIFRPC Handler (`0x1E68`):**
  - `0x1EA8`: checks `fno == 255`.
  - `0x1EFC`: returns pointer `$v0 = 0x3330`.
  - `0x1F08`: loads `$a0` from `.data` `0x323C` (value `0x30303033`, ASCII `"3000"`).
  - `0x1F0C`: sets `$v1 = 2`.
  - `0x1F10`: stores `$v1` (2) to `reply[1]`.
  - `0x1F18`: stores `$a0` (`0x30303033`) to `reply[0]`.

### 3. EE Client Consumption (`SCUS_971.77`, Downhill Domination)

Boot ELF SHA-256 `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`:

- `0x0025D8E4`: issues `sceSifCallRpc` with `fno = 255`, `send_size = 8`, `recv_size = 8`.
- `0x0025D930..0x0025D93C`: copies `reply[0]` into `0x00764CA8`.
- `0x0025D940..0x0025D970`: loads `reply[1]`, verifies `reply[1] == 2`, and writes
  flags `1` to `0x002821EC` and `0x002821F0`.
- `0x0025D9C4..0x0025D9D4`: helper function at `0x0025D998` calls
  `memcmp(0x00764CA8, 0x00282144, 4)` where `0x00282144` holds ASCII `"2700"`
  (`0x30303732`).
- When the comparison matches, the helper returns `0`, and the caller at `0x0025DADC`
  branches to `0x0025DAF8` (the main execution path after initialisation).

## Derived

1. Function 255 is the standard Sony `sceFileioInit` initialisation handshake.
2. Reply word 0 is the 4-byte ASCII IOP module revision (`"2700"` for IOPRP 2.7.0,
   `"3000"` for IOPRP 3.0.0).
3. Reply word 1 is the FILEIO interface version flag (integer `2`), indicating
   FILEIO version 2 capability.
4. Serving reply `[0x30303732, 2]` satisfies both the `reply[1] == 2` check and
   the `memcmp(0x00764CA8, "2700", 4)` verification in Downhill Domination.
5. All file operations (fnos 0..254: open, close, read, write, lseek, etc.)
   remain unmodelled and fail-closed.

## Inferred

- Black links SDK 3.0.0 libraries (`PsIIlibgraph3000`, `PsIIlibdma3000`, etc.)
  and its own custom stream driver `GTFSCDVD.IRX`, never issuing FILEIO calls.
  Downhill Domination is the only evidenced title using FILEIO during early boot.

## Unknown

- File descriptor table allocation and guest-visible filesystem queries for
  individual file open operations. These will be evidenced and modelled when a
  title issues an unmodelled fno (e.g. `open = 0`).

## Decision

1. In `serve_ee_fileio_call`, serve function 255 (`kEeFileioInitFno`):
   - Require `call.send_size >= 8`, `call.recv_size >= 8`, `call.recv_addr != 0`,
     and `call.send_addr != 0`.
   - Store `kEeFileioVersionWord2700` (`0x30303732`) to `call.recv_addr`.
   - Store `kEeFileioInterfaceVersion` (`2`) to `call.recv_addr + 4`.
   - Return `true`, triggering `RPC_END` packet delivery and semaphore release.
2. Refuse any malformed call or any other function number (`0..254`), returning
   `false` with no reply mutation.
3. Keep all bounded diagnostic fields in `EeSifCmdState` intact and recorded.
   Do not alter the binary layout of `EeState` or `EeSifCmdState`.

## Consequences and Observed Frontier

With `fno 255` served, Downhill Domination advances beyond `WaitSema(11)`:
- `sceFileioInit` completes and marks initialization successful (`0x002821EC = 1`, `0x002821F0 = 1`).
- The caller continues to GS display environment setup (`0x0020B9B8`), which queries the console
  video standard and ROM version via `0x00243680` -> `0x002436EC` -> `0x002435D0`.
- The guest calls `sceOpen("rom0:ROMVER", SCE_RDONLY)` (`fno 0`), creating semaphore 12 (`CreateSema`).
- `fno 0` is unmodelled and refused, leaving the guest blocked at `WaitSema(12)` (`pc=0x0025A724`).
- Total executed instructions advance from 4,586,235 to 4,587,011 (+776 instructions).
- Syscalls seen/handled advance from 1221/1203 to 1228/1210 (+7 syscalls).
- The new guest frontier is FILEIO `fno 0` (`sceOpen`), requesting `rom0:ROMVER`.

