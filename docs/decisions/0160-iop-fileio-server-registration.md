# ADR 0160: Register IOP FILEIO Server (0x80000001)

- **Status:** Accepted.
- **Date:** 2026-11-17.
- **Scope:** Server registration in `runtime/include/reps2/runtime/ee_hle.hpp` and max server bound in `ee_state.hpp`.

## Context

During retail title boot, `sceFileioInit` at `0x0025D790` registers SIF command handlers `0x80000011` and `0x80000013`, and then attempts to bind to the IOP FILEIO server (`0x80000001`).

- **Observed:** The EE caller executes `sceSifBindRpc` targeting server ID `0x80000001`. Because this ID was not present in the modelled server table, the bind returned null/zero handle (`had = 0`). The title entered a 10,000-iteration retry delay loop at `0x0025D820`, repeatedly calling `sceSifBindRpc` and stalling progression.
- **Derived:** Registering the FILEIO server in `initialize_hle_boot_environment` satisfies the bind check, causing the initialization loop to exit immediately on the first pass.
- **Unknown:** Specific file I/O operations (open/read/write/close) which remain unmodelled and fail closed when called.

## Decision

1. Raise `kEeMaxSifRpcServers` from 18 to 24 in `ee_state.hpp` to ensure sufficient room for FILEIO and future servers.
2. Define `kEeSifServerFileIo = 0x80000001U`.
3. Provide `serve_ee_fileio_call` which records and refuses individual unmodelled file commands.
4. Register the server in `initialize_hle_boot_environment`.

## Consequences

The `sceFileioInit` bind loop succeeds immediately, allowing the title to advance past file I/O initialization.
