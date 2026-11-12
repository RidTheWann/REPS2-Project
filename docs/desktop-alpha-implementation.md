# Desktop Alpha implementation status

Updated: 2026-11-11

## Scope delivered

The \`desktop/\` solution is the first production-code Windows shell. It uses WinUI 3 + .NET 10 and follows the repository's approved process boundary: the UI invokes the Rust frontend and RG REPS2 ENGINE as child processes and reads versioned artifacts. It does not link native engine libraries or introduce an FFI surface.

### Implemented

- S1 Library / S2 empty state: native shell, content-addressed library scan, title/serial/disc/status search, evidence-state filters (needs review, ready to build, native-built+, playable only), alphabetical sorting, result counts, recoverable no-match state, lifecycle labels and title details.
- S3 Add Game: Windows ISO picker → \`reps2 add-game <iso> --library <root>\`; the CLI remains the authority for ISO parsing and atomic library writes.
- S5 Game Detail: source ISO/ELF digests, entry point, analysis coverage, native build identity and next valid action.
- S12 recovery increment: relink a moved source ISO only when its exact SHA-256 and recorded byte size match the existing content-addressed game entry; update only the source path with same-directory atomic replacement.
- S7 Build Queue: durable local job records, live stage/status updates, job messages, exit codes, and output/diagnostics directory access.
- S9 Developer Center: \`reps2 dev capabilities|sessions|inspect latest|summary latest\`.
- S11 Settings: library/toolchain/logging/build defaults and opt-in startup update checks (enabled by default).
- P1 native build orchestration: \`recompile-iso\`, CMake configure/build, and module/cache/ISO/ELF provenance verification.
- Bounded execution probe with saved \`rg.reps2.engine.run_report.v1\` output.
- Update detector scans only published \`desktop-v*\` GitHub prereleases and compares semantic versions.
- One-click update downloads the fixed Windows package plus checksum, verifies SHA-256, and hands off to a separate self-contained helper outside the app folder. The helper waits for the UI to exit, safely extracts, performs a same-volume staging/backup swap, restarts the application, waits for a startup handshake, and rolls back if the updated app does not start successfully.
- CI publishes a GitHub prerelease with the ZIP and SHA-256 sidecar when a \`desktop-v*\` tag is pushed.

### Deliberately deferred / not claimed

- No title is Boot-tested or Playable. There is no Play button, gameplay acceptance editor or automatic compatibility promotion.
- S8 is a bounded diagnostic probe, not a persistent live gameplay session.
- Delete, cache reuse, concurrent jobs, saves, cover metadata, controller parity and MSIX signing/Store distribution remain open.
- Full UI Automation, Narrator/NVDA and 200% scaling sign-off remain open.
- Native title builds require external CMake + MSVC Build Tools; these are not bundled in the desktop app.
- Run reports never mutate title compatibility state.

## Update/release contract

- The public update feed is the GitHub Releases API for \`RidTheWann/REPS2-Project\`.
- Only prereleases whose tags start with \`desktop-v\` are considered.
- Required assets are exactly \`REPS2-Desktop-Alpha-windows-x64.zip\` and its \`.sha256\` sidecar.
- The UI verifies the sidecar and (when exposed by GitHub) the release asset digest. The helper verifies the downloaded archive again before extraction.
- ZIP extraction is manual and enforces path containment, duplicate-path rejection, symlink rejection, entry-count and expanded-size limits.
- The app is installed per-user under \`%LOCALAPPDATA%\\Programs\\REPS2\`; data remains under \`%LOCALAPPDATA%\\REPS2\`.
- A startup signal is written after the new UI has initialized and navigated. Failure to receive it within the deadline triggers rollback to the previous folder.
- SHA-256 is integrity verification, not publisher authentication. Authenticode signing is deferred.

## Test contract

- Core tests cover Imported / Analyzed / Translation incomplete / Translated / Native-built / Recovery-required derivation.
- Build identity can only promote a translated title when module, identity, cache key and provenance match the ISO and boot ELF digests.
- Library tests cover evidence-aware filters, sorting and search composition; recovery tests cover exact SHA-256/size relink and invalid identities; process tests cover cancellation of a long-running child.
- Update tests cover semantic-version ordering, ordinary extraction, zip-slip paths, absolute/drive-qualified paths, duplicate names and symbolic links.
- Windows CI builds the WinUI app and updater, runs the core tests, assembles the CI-verified SDK and uploads a portable package.

## Release gate

This is a developer-facing Windows Desktop Alpha. It does not meet the public beta candidate or REPS2 1.0 gates in \`docs/RELEASE_GATES.md\`.
