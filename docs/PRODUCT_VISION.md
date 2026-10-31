# REPS2 product vision

## Product definition

REPS2 is intended to become a controller-first, multi-game PlayStation 2 platform for Windows PC. The target experience is closer to Steam Big Picture than to a developer command-line utility:

1. The user selects an owned PS2 ISO.
2. REPS2 identifies the disc and boot executable automatically.
3. REPS2 analyzes and statically recompiles supported guest code.
4. REPS2 links the generated native module with its PS2 runtime.
5. The game appears in a persistent library and can be launched from the UI.
6. Subsequent launches reuse validated per-game build artifacts and runtime state.

The intended user input is the ISO. Users should not need to manually extract BOOT.ELF, prepare a game-specific directory, or understand ELF, IR, compiler, or runtime details.

## Multi-game is a platform property

REPS2 must not become one hand-written recompiler per game. The core disc reader, ELF loader, R5900 decoder, analysis, typed IR, C++ backend, build system, runtime ABI, and host services should be shared across titles.

Per-game records hold only data that is genuinely title-specific: disc identity, boot executable identity, analysis coverage, unresolved behavior, build artifacts, compatibility notes, settings, saves, and measured validation evidence. A game-specific workaround must be isolated, documented, and tested; it must not silently fork the instruction semantics or core runtime.

A single real game can be the first vertical-slice target, but it is a test target, not the architecture. The long-term compatibility catalog must grow across multiple titles and revisions.

## End-to-end product pipeline

```
Owned ISO
  -> content fingerprint
  -> ISO 9660 / SYSTEM.CNF discovery
  -> EE ELF validation and load-map construction
  -> R5900 function discovery and CFG recovery
  -> typed IR and semantic verification
  -> deterministic native-code generation
  -> host C++ compile and link
  -> shared PS2 runtime + title module
  -> game session
  -> controller-first launcher
```

The launcher is the final product surface, not the engine. The engine must expose stable APIs and structured state transitions so the UI can display import, analysis, build, launch, crash, and compatibility states without interpreting log text.

## PS2 runtime requirements

Static recompilation translates CPU code; it does not turn the PS2 hardware environment into a normal PC process. Recompiled code still expects the machine state and services it used on the console. The runtime therefore needs explicit, independently testable subsystem boundaries for:

- Emotion Engine core: R5900 integer/MMI/FPU/COP0 behavior, 128-bit GPR semantics, exceptions, interrupts, address translation, timers, and main/scratchpad memory.
- Vector Units: VU0 macro mode, VU0/VU1 microprogram execution, VIF command processing, vector memory, and synchronization.
- Graphics path: DMA channels, GIF packet processing, GS registers/state, local GS memory, rendering, and presentation.
- I/O system: IOP/R3000A execution, IOP RAM, IRX module loading, SIF/SIF RPC, CD/DVD services, controllers, memory cards, and SPU2 audio.
- Host integration: windowing, input mapping, audio output, filesystem-backed optical media, save storage, diagnostics, and clean shutdown.

These boundaries must preserve observable guest behavior. Returning success for an unimplemented device operation is not acceptable. Unsupported behavior should produce a precise compatibility diagnostic or controlled guest-visible failure.

The PS2 SDK documentation describes separate EE and IOP software areas; its architecture material also explains that the IOP mediates peripherals and communicates with the EE through SIF. This is why the runtime cannot be reduced to the EE instruction recompiler alone. The project will use independently developed implementations and public behavioral evidence; it will not copy third-party emulator or SDK source.

## BIOS and user-owned content

The desired UX should not require users to locate or install a separate proprietary BIOS image. REPS2 should implement the necessary boot/kernel-facing behavior through clean-room high-level emulation where technically and legally appropriate. It must not distribute Sony firmware, proprietary SDKs, game executables, or game assets.

The original ISO remains user-owned. The current library backend stores its canonical source path instead of copying a multi-gigabyte image; if the file is moved, the eventual UI must offer an explicit relink operation and must never bind the record to a different disc silently.

## Game identity and build-cache correctness

An ISO content digest identifies an exact input image. A native build cache must use more than the title serial: the cache key must include at least the ISO digest, REPS2 frontend/compiler version, IR/manifest schema, runtime ABI version, host target, compiler identity, and relevant build configuration. A same-serial revision or modified disc image must not reuse stale generated code.

The per-game manifest must distinguish these states:

- **Imported:** disc and boot ELF validated and stored.
- **Analyzed:** executable ranges and CFG coverage recorded.
- **Translated:** a documented subset has typed IR/codegen output.
- **Native-built:** generated code has been compiled and linked with a specific runtime ABI.
- **Boot-tested:** a repeatable title-specific boot stage has been reached and evidenced.
- **Playable:** the title passes a defined gameplay/session acceptance set.

A previous stage never implies a later one. In particular, a parser pass, a generated C++ file, or a successful host compile does not mean a title is playable.

## Desktop experience

The future launcher should support a game grid/list, controller-first navigation, search and filtering, per-game status and settings, play/stop, logs/crash reports, save paths, and a straightforward Add Game workflow. Metadata and cover art should be optional enrichment; library operation must not depend on scraping services or network access.

The desktop shell should call the same library/session API as the CLI. This ensures automated tests can exercise the exact path the UI uses and avoids implementing a parallel GUI-only game pipeline.

## Development sequence

1. Managed one-ISO import and stable per-game manifest (current first library slice).
2. Persistent library operations, content verification, relink/rescan, and versioned manifest migrations.
3. Complete executable analysis and broad R5900/MIPS EE semantic coverage.
4. The first real-title native boot slice with traceable runtime-service requirements.
5. IOP, VU, DMA/SIF, GS, audio, media, and input services expanded by observed title needs.
6. Multi-title compatibility testing, per-game build caching, profiling, and regression automation.
7. Controller-first desktop shell, packaging, settings, diagnostics, and stable releases.

This order intentionally prioritizes a genuine end-to-end native boot path before building a polished launcher that would otherwise front an engine that cannot yet run games.

## Compatibility and evidence policy

Every compatibility claim must be tied to a disc digest/revision, REPS2 revision, runtime configuration, host platform, and repeatable test evidence. Synthetic fixtures validate isolated semantics; they do not establish compatibility with a retail title. A compatibility record must preserve the distinction between unsupported instructions, unresolved dynamic control flow, missing runtime services, and observed title-specific defects.
