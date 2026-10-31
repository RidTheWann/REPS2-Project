# ADR 0010: Content-addressed multi-game ISO library

- Status: Accepted
- Date: 2026-10-31

## Context

REPS2 is intended to be a multi-game native PS2 platform, not a tool that expects users to manually prepare extracted ELF files or organize a developer workspace. A user should be able to add one owned ISO and let REPS2 discover the boot executable and create a managed game record.

A game serial alone is not a safe cache identity: revisions, regional releases, modified images, and bad dumps can share a serial. Copying every multi-gigabyte ISO into an application library also creates unnecessary storage duplication and can surprise users.

## Decision

1. The import boundary is one ISO per operation: `reps2 add-game <iso> <library-root>`.
2. The Rust frontend streams SHA-256 over the source image and derives the immutable record id from the content digest.
3. The import code opens the source ISO read-only, resolves SYSTEM.CNF BOOT2 then BOOT, validates the target as an ELF32 little-endian EM_MIPS ET_EXEC, and extracts it internally.
4. Each record is stored under `<library-root>/games/sha256-<digest>/` with a schema-versioned `game.json`, `boot.elf`, and optional generated artifacts.
5. The ISO is referenced by canonical source path, not copied. A future library UI must provide explicit relinking if the path disappears.
6. Import publication uses a staging directory and final rename. Re-import of unchanged content is idempotent and verifies the stored ELF before trusting the existing entry.
7. The manifest reports analysis/code-generation coverage, loadable segments, unresolved behavior, and optional artifact paths.
8. `playable` remains false until a complete runtime is integrated and a repeatable title-stage acceptance test demonstrates actual execution.

## Consequences

- Different ISO byte streams get different records even when their title serials match.
- The user does not need to extract BOOT.ELF or place ISOs in a special project folder.
- Library records are independently discoverable by the future UI and do not require a mutable central database.
- Moving an ISO requires a relink operation that is not implemented yet.
- The initial import backend is not yet a launcher, runtime, or proof of a playable game.
- SHA-256 is implemented with standard known-answer test vectors and chunk-boundary tests; no third-party Rust dependency or lockfile change is introduced.
