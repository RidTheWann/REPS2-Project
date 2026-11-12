# REPS2 project state

## Repository recovery (2026-11-13)

The workspace at `D:\REPS2-Project` contained no `.git` directory and the GitHub repository `RidTheWann/REPS2-Project` was empty (`isEmpty: true`, `pushedAt: 2026-11-12T19:12:57Z`, no commits). The local backup `C:\Users\RidTheWann\REPS2-Backups\REPS2-Project-20261104-045206.bundle` and its bare `.git` restored 562 commits through `4bcbaf4` (2026-11-04, PR #71). The working-tree content documented below as `main` `431f71d` (2026-11-11) is preserved in the recovery commit; commits between PR #72 and PR #82 were not in any local object store and are not individually recoverable. A byte-level inventory against the backup found `.github/workflows/ci.yml` as the only missing tracked file; it was restored identically from the backup. Post-recovery local verification: `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked`, CMake Release build, `ctest -C Release` (40/40), and desktop core tests (31/31). GitHub Releases, Pages, and remote history from the former repository were not recoverable.

Updated: 2026-11-11 (main @ 431f71d)

## Objective

Advance the multi-title PlayStation 2 static AOT recompiler toward a repeatable, guest-visible boot stage from an owner-supplied ISO. Downhill Domination USA (SCUS_971.77) is the current M4 vertical-slice target; it is evidence for the shared toolchain, not a title-specific product architecture.

The architectural split remains:
- Rust frontend: ISO/ELF parsing, R5900 decoding, discovery/CFG, typed IR, verification, deterministic C++ generation, manifests and CLI.
- C++20 runtime: EE state and memory, MMIO/device models, shared generated-code ABI and future PS2 subsystems.
- Generated native title module + manifest: explicit, versioned boundary between frontend output and shared runtime.

The product goal remains broader than one title and broader than an ELF-launch utility. Importing an ISO, extracting BOOT.ELF, translating functions, building C++ or passing synthetic tests does not establish that a PS2 game boots or is playable.

## Verified repository state

- Default branch: `origin/main` at `431f71d8238ffcd16faaf64cec7ec162c8bb4fad` (11 November 2026). PRs #79–#82 are merged and no open PRs were present at audit time. PR #79 registers the NCmd/SearchFile RPC servers and serves NCmdDiskReady; PR #80 admits control-flow targets entering delay slots; PR #81 scans declared relocated-code ranges during function discovery; PR #82 adds the 32-lane GPR snapshot to the RG REPS2 ENGINE run report (ADR 0128).
- Current open pull requests and CI are live repository state; re-check them for each task rather than relying on this snapshot.
- Merged since PR #23: PR #24 records the final main head after PR 23; PR #25 adds session-scoped developer diagnostics; PR #26 adds the agent-readable developer CLI; PR #27 instruments frontend pipeline stages; PR #28 adds the configuration-specific native build identity; PR #29 implements MMI0 PEXT5/PPAC5; PR #30 rejects ELF BSS-extension overlaps; PR #31 bounds coverage words to executable segment bytes; PR #32 keeps speculative CFG limits fatal (ADR 0085); PR #33 validates section alignment and adds the deterministic synthetic ELF fixture builder; PR #34 reconciles project-state documentation; PR #35 accepts GNU ld trailing local symbols past `sh_info`; PR #36 publishes developer capability discovery and the diagnostic-code catalog (ADR 0086); PR #37 models the DMAC register file and the HLE boot state (ADR 0091); PR #38 adds the structured session summary (ADR 0090); PRs #39–#45 add SIF register/DMAC HLE services and PS2 ELF compatibility diagnostics; PR #46 adds branch differential testing; PR #47 completes native build identity provenance; PR #48 implements EE-side SIF register/DMA services and a bounded IOP SIFCMD model (ADR 0098); PR #49 strengthens branch differential tests and fixes mutation accounting.
- PR #48 was verified on Windows with Rust gates and CMake Release/CTest; its real-title probe was not rerun at merge time.
- PRs #51–#59 are merged; the prior stacked branches and review state no longer describe the current repository. See the merge history above and the exact-head CI records in GitHub.
- Local verification on Windows for the IOP SIF RPC bind reply (ADR 0104, from `origin/main` `ca0b39b`): `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked`, CMake Release build, and `ctest -C Release` (34/34) all pass, including `reps2_hle_syscall_codegen_smoke` (96 cases, three new) and `reps2_native_build_driver_smoke`. The real-title probe was regenerated on this exact revision and **did** change: the bind wait now completes. Its trace is recorded below.

- Local verification on Windows for the DMAC register naming and interrupt condition (ADR 0105, from `origin/main` `79c9d8d`): CMake Release build and `ctest -C Release` (34/34) pass, including `reps2_dmac_dstat_codegen_smoke` with the new interrupt-condition case. The change renames state and MMIO handlers only (`D_CTRL` at `0x1000E000`, `D_PCR` at `0x1000E020`) and adds a pure predicate, so the real-title probe trace is expected to be identical; it was re-run to confirm.

- Local verification on Windows for `PollSema` (ADR 0119, branch `feat/runtime-semaphore-poll`, from `origin/main` `6756d0e`): Rust fmt/clippy/test, CMake Release build and `ctest -C Release` (40/40) pass, including `reps2_hle_syscall_codegen_smoke` (118 cases, four new). Windows x64/MSVC 19.51.36260.0 real-title probe on boot ELF SHA-256 `adfda7b7...f0cf0c` and parent ISO SHA-256 `0fe2b777...a7eb86` (`D:\REPS2-DevTests\DownhillDomination\PostPR68`): the service is now handled (`hle_name=PollSema`, `hle_handled=26574`) and the run reaches 4,999,998 instructions, but the title spins in a poll loop on semaphore id 5 (`poll_sema=26479 poll_sema_acquired=0 poll_sema_count_before=0 sema_op_id=5`, `syscalls=26591`) and exhausts its budget at `pc=0x00246DAC`. No additional SIF RPC traffic occurs (`sif_rpc_bind=2 sif_rpc_call=2`, unchanged from the previous revision), so the count is never raised: the awaited producer is an event REPS2 does not model yet. This is a waiting-on-an-unimplemented-producer boundary, not an unimplemented service, and not a boot or playable stage.

- Local verification on Windows for the `PollSema` ID-result correction (PR #69 merged as `a5268b4`, from `origin/main` `29eca80`): Rust fmt/clippy/test, CMake Release build and `ctest -C Release` (40/40) pass; `reps2_hle_syscall_codegen_smoke` has 118 cases, and the new nonzero-ID regression failed under the previous return-0 behavior at assertion 942 before passing. The PostPR69 observation probe had recorded `poll_sema_first_count=1 poll_sema_acquired_total=2`, proving the lock did hold a count, and the retail wrapper at `0x00246FA0` compares `$v0` against the passed id. Windows x64/MSVC 19.51.36260.0 real-title probe on the same boot ELF and ISO hashes (`D:\REPS2-DevTests\DownhillDomination\PostPR70`): one poll consumes the count (`poll_sema=1 poll_sema_acquired=1 poll_sema_first_count=1 poll_sema_acquired_total=1`), the run leaves the loop, and it stops as `unhandled_syscall` service `0x30` at `0x0025A5E4` after 3,172,922 instructions with `$a0 = 1` and `$a1 = 0x007607D8`. That is a runtime service boundary, not a boot or playable stage.

- Local verification on Windows for `ReferThreadStatus` (ADR 0120, branch `feat/runtime-refer-thread-status`, from `origin/main` `a5268b4`): Rust fmt/clippy/test, CMake Release build and `ctest -C Release` (40/40) pass, including `reps2_hle_syscall_codegen_smoke` (121 cases, three new). Windows x64/MSVC 19.51.36260.0 real-title probe on boot ELF SHA-256 `adfda7b7...f0cf0c` and parent ISO SHA-256 `0fe2b777...a7eb86` (`D:\REPS2-DevTests\DownhillDomination\PostPR71`): the service is handled (`refer_thread_status=1 refer_thread_status_id=1 refer_thread_status_value=0x00000001 refer_thread_status_outcome=none`), the single `PollSema` acquisition stands, and the run executes the full 5,000,000-instruction budget (`executed=4,999,995`, `syscalls=118`, `hle_handled=101`), stopping as `budget_exhausted` at `pc=0x002476B8` in the title's bind-retry loop. The loop retries the SIF RPC bind for server `0x80000593` (`sif_rpc_bind=3 sif_rpc_bind_sid=0x80000593 sif_rpc_bind_server=0x00000000 sif_rpc_bind_had=0`), which the resident model answers as "no server"; the SIF command traffic shows `sif_cmd_pkts=10 sif_cmd_cid=0x80000009 sif_set_dma=10 sif_ee_inbound=7`. This is a missing-server boundary, not a boot or playable stage.

- Local verification on Windows for the CDVDFSV S-command server registration (ADR 0121, branch `feat/runtime-cdvd-scmd-bind`, from `origin/main` `17146e7`): Rust fmt/clippy/test, CMake Release build and `ctest -C Release` (40/40) pass, including `reps2_hle_syscall_codegen_smoke` (122 cases, one new). Windows x64/MSVC 19.51.36260.0 real-title probe on boot ELF SHA-256 `adfda7b7...f0cf0c` and parent ISO SHA-256 `0fe2b777...a7eb86` (`D:\REPS2-DevTests\DownhillDomination\PostPR72`): the bind for `0x80000593` now completes (`sif_rpc_bind=3 sif_rpc_bind_sid=0x80000593 sif_rpc_bind_server=0x80000593 sif_rpc_bind_had=1`), the `ReferThreadStatus` and single `PollSema` steps stand, and the title issues its first S-command `sif_rpc_call_number=34` (0x22 `CD_SCMD_MMODE`) with `rpc_call_req=0x00000002` (`SCECdMmodeDvd`) and 4-byte send/recv sizes. The handler refuses it, so nothing is answered and the caller blocks (`stage=semaphore_wait_blocked`, `pc=0x0025A724`, 3,173,557 instructions, `sif_rpc_call=3 sif_rpc_call_replies=2 sif_rpc_call_replied=0`). This is a modelled-boundary stop, not a boot or playable stage; the next unit is the S-command body.
- Local verification on Windows for the `CD_SCMD_MMODE` S-command (ADR 0122, branch `feat/runtime-cdvd-scmd-mmode`, from `origin/main` `8958cc4`): Rust fmt/clippy/test, CMake Release build and `ctest -C Release` (40/40) pass, including `reps2_hle_syscall_codegen_smoke` (124 cases, two new). Windows x64/MSVC 19.51.36260.0 real-title probe on boot ELF SHA-256 `adfda7b7...f0cf0c` and parent ISO SHA-256 `0fe2b777...a7eb86` (`D:\REPS2-DevTests\DownhillDomination\PostPR73`): the first S-command is answered (`scmd_calls=1 scmd_replies=1 scmd_number=34 scmd_media=0x00000002 scmd_retres=0x00000001 scmd_replied=1 cdvd_mmode=0x00000002`, `sif_rpc_call_replied=1`), the caller is released, and execution runs to the full budget (`stage=budget_exhausted`, `executed=4,999,997`, `pc=0x00247AD0`) instead of blocking at `0x0025A724`. The IOP-side reference (`cdvdman.c sceCdMmode` = `g_cdvdman_mmode = media; return 1;`) matches the HLE: store the media word, answer `m_retres = 1`. This is a modelled-boundary advance, not a boot or playable stage; the next frontier is the loop at `0x00247AD0`.

- Local verification on Windows for the engine-session HLE boot environment (ADR 0123, from `origin/main` `2cb1fd2`): Rust fmt/clippy/test, CMake Release build and `ctest -C Release` (40/40) pass, with `rg_reps2_engine_smoke` now asserting that an HLE-enabled session seeds `dmac.d_stat_known` and a non-HLE session does not. Before the fix, the real-title module run through `rg-reps2-engine --hle-syscalls` stopped as `unsupported_memory_access` at `pc=0x0025C274` (a `D_STAT` read at `0x1000E010`) after 3,168,708 instructions, while the generated probe reached `budget_exhausted` at `0x00247AD0` with 4,999,997. After the fix, the engine runner on the same exact-main module (`2cb1fd2`, Downhill Domination USA, boot ELF SHA-256 `adfda7b7…f0cf0c`, ISO SHA-256 `0fe2b777…a7eb86`) reaches `budget_limit_reached` at `pc=0x00247AD0` after 79,999,922 instructions over 16 batches, with the trace confined to the `0x00247AD0..0x00247AE4` delay loop ahead of the `sceSifBindRpc` call for sid `0x8000059C` (CDVDFSV DiskReady). Black (USA) rerun on the same revision stops as `guest_stopped` at `0x80076000` after 479,362 instructions (runtime-copied code, undeclared for that title). These are modelled-boundary results, not boot or playable stages.

- Local verification on Windows for the modelled IOP reset completion (ADR 0118, PR #67 merged as `6756d0e`): `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked`, CMake Release build and `ctest -C Release` (40/40) pass, including `reps2_hle_syscall_codegen_smoke` (114 cases, two new: the reset→SMFLAG→INIT_CMD handshake and truncated-packet no-mutation). Windows x64/MSVC 19.51.36260.0 real-title probe on boot ELF SHA-256 `adfda7b7...f0cf0c` and parent ISO SHA-256 `0fe2b777...a7eb86`: `iop_resets=1 iop_resets_completed=1 last_reset_mode=0 last_reset_arg_len=32 sif_smflag=0x00070000`, RPCINIT repeated (`sif_rpcinit_sent=1 sif_ee_inbound=6 sif_ee_sreg0=1`), a second CdInit bind/call completed (`sif_rpc_bind=2 sif_rpc_call=2 sif_rpc_call_replies=2 sif_rpc_call_server=0x80000592`), then `unhandled_syscall` `0x45` (`PollSema`) at `0x0025A734` after 3,172,908 instructions. The prior revision spent the whole 5,000,000-instruction budget in the `SifIopSync` `SMFLAG`/`BOOTEND` poll at `0x0025ED10`. This is a post-reset runtime boundary, not a boot or playable stage; the requested `IOPRP270.IMG` image is never executed and its services stay absent.

- Local verification on Windows for the SIF0 teardown services (ADRs 0116/0117, from `origin/main` `cf34cf2`): Rust gates and CMake/CTest pass, including `reps2_hle_syscall_codegen_smoke` (112 cases, six new). The regenerated real-title probe reports `remove_dmac=1`/channel 5/id 5/`removed` and `sif_stop_dma=1 sif_stop_dma_cleared=0`, processes the reset packet (`sif_cmd_cid=0x80000003`), and then exhausts the full 5,000,000-instruction budget in the post-reset `SifIopSync` `SMFLAG`/`BOOTEND` poll at `0x0025ED10`; the modelled IOP is completed one revision later by ADR 0118.

- Local verification on Windows for `_DisableDmac` (ADR 0115, from `origin/main` `13976ae`): Rust gates and CMake/CTest pass, including `reps2_hle_syscall_codegen_smoke` (100 cases, four new). The regenerated real-title probe reports `disable_dmac=1 disable_dmac_channel=5 disable_dmac_outcome=disabled` and advances 26 instructions to `unhandled_syscall` `0x13` (`RemoveDmacHandler`, channel 5, id 5) at `0x0025A414` after 3,170,312 instructions.

- Local verification on Windows for the SIFRPC server table (ADR 0114, from `origin/main` `50e6c5d`): `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace --locked`, CMake Release build and `ctest -C Release` (40/40) pass, including `reps2_hle_syscall_codegen_smoke` (102 cases, six new) and `rg_reps2_engine_smoke`. The regenerated real-title probe (boot ELF SHA-256 `adfda7b7…f0cf0c`, ISO SHA-256 `0fe2b777…a7eb86`) now completes the CdInit bind and call — `sif_rpc_bind=1 sif_rpc_bind_sid=0x80000592 sif_rpc_bind_signalled=1 sif_rpc_bind_had=1`, `sif_rpc_call=1 sif_rpc_call_replies=1 sif_rpc_call_number=0 sif_rpc_call_server=0x80000592 sif_rpc_call_send=4 sif_rpc_call_recv=16 sif_rpc_call_replied=1` — and stops at `unhandled_syscall` `0x17` (`_DisableDmac`, `$a0 = 5`) at `0x0025A454` after 3,170,286 instructions. The previous revision exhausted its 5,000,000-instruction budget in the bind retry loop at `0x002478F0`.
- Local verification on Windows for the media-ordering fix (ADR 0108): the same CMake/CTest run includes `rg_reps2_engine_smoke`, whose new module initializer observes the host source and reads one synthetic sector.

- Local verification on Windows for the host media boundary (ADR 0106, from `origin/main` `509100e`): CMake Release build and `ctest -C Release` (35/35) pass, including the new runtime-only `reps2_host_media_smoke`. No device model consumes the boundary yet, so the real-title probe is unchanged by construction.
- PR #50 completed: the CLI now binds a native build to a verified parent ISO identity (`--parent-iso`) and surfaces the ELF compatibility report; the prebuilt binary in older checkouts lacks the option until rebuilt from `046e063`.
- `docs/ROADMAP.md` still tracks the M4 exit gate as open. Nothing here establishes a real-title boot stage.

Re-read the actual GitHub default-branch head and live PR/issue state for every later task rather than treating this timestamped snapshot as permanent.

### Latest verified main snapshot (2026-11-11)

- `main`: `431f71d8238ffcd16faaf64cec7ec162c8bb4fad`; no open pull requests at audit time.
- Latest GitHub Actions CI run: `42978407701`, conclusion `success`. Rust MSRV 1.88, Linux, Windows/MSVC, progress-dashboard generation, and Windows Desktop Alpha jobs completed successfully.
- This is repository/CI evidence only. The current compatibility matrix still has no guest-visible boot-tested or Playable title.
- Roadmap checklist: 138/182 items checked (75.8%). This is checklist completion, not game compatibility or release readiness.
- The latest M4 evidence from PR #82 is a Windows/MSVC bounded probe on PR head `d4496a7`, not a fresh probe on merged main. It stops at `0x800761A4` after 3,890,274 guest instructions on `lw $20, 0($2)`; the GPR snapshot identifies `$2 = 0xFFFFFFFFB0001800`. ADR 0129 corrects the address classification to EE Timer 3 Count (`T3_COUNT`), not DMAC.

## RG REPS2 ENGINE dynamic module slice (PR #61)

The open engine branch extends the initial synchronous session core with a separately compiled native-module contract. The exported entry is a single C-linkage symbol, `rg_reps2_get_native_module_descriptor_v1`, returning a fixed-layout POD descriptor (descriptor ABI v1, engine ABI v2). The descriptor carries the runtime ABI, compiler/standard-library/runtime-layout fingerprint, target identity, bounded module name and opaque-state initializer/execute callbacks. No STL object or C++ reference crosses the exported module boundary.

The host loader canonicalizes an explicit library path and accepts only a regular file. Windows uses `LoadLibraryExW` with restricted DLL search flags; ELF-family builds use `dlopen(RTLD_NOW | RTLD_LOCAL)` and resolve only the documented descriptor symbol. Admission is transactional and validates descriptor size/version/reserved fields, name syntax/length, engine/runtime/toolchain ABI, target and callback presence. Callback status values are checked before conversion.

A native shared module is executable code with the application's process privileges. The loader is not a sandbox and does not authenticate the publisher or verify a signed package. Only trusted modules may be loaded. Dynamic libraries are intentionally pinned until process exit; no `FreeLibrary`/`dlclose` or hot-unload is attempted. Prepared handles own a copy of the module name and retain the library token; sessions retain their own copy so the caller can release its handle after start.

The Rust frontend's `--native-build` path now writes `rg_reps2_module.cpp` and emits a shared-library CMake target using the same generated dispatcher/part files as its host probe. The wrapper exports the v1 POD descriptor and adapts the generated initializer/CFG executor through opaque-state callbacks. The native build writes a configuration-specific `reps2-native-module-<config>.path` pointing to the exact DLL/SO artifact. CMake/CTest builds that generated artifact, invokes the existing engine loader smoke executable against it, creates an EngineSession, drops the caller's prepared handle, and proves the session still executes guest instructions. The separate small loader fixture retains negative coverage for missing descriptor symbols and paths plus transactional handle preservation. This end-to-end generated-module integration is required to pass on Windows/MSVC and Linux/GCC CI. Android/macOS loader branches remain unvalidated; AArch64 code generation, signing/trust policy, hot-unload, real-title boot and performance improvement are not claimed. See ADRs 0107, 0108 and 0109.

## RG REPS2 ENGINE bounded host runner (PR #61)

The branch now adds the standalone C++20 `rg-reps2-engine` executable linked against `RG::REPS2Engine`. It accepts an explicit trusted native module path, optional exact 32 MiB initial EE RAM image, optional raw 2048-byte-sector ISO media source, guest instruction budget per batch, a maximum batch count, optional EE syscall HLE and optional bounded instruction trace. Budget exhaustion resumes the same EngineSession state until a terminal guest status or the configured batch cap.

The runner emits one versioned `rg.reps2.engine.run_report.v1` JSON object with module/target identity, input/configuration, final phase and PC, total instruction accounting, elapsed host time, per-batch guest statuses/budgets/PCs/counters and optional trace PCs. Host errors emit `rg.reps2.engine.error.v1` with stable codes. A zero process exit means the host produced a bounded run report; it does not mean a game booted or is playable.

CMake builds and installs the runner. CI invokes its help and version paths, then tests error JSON/exit-code contracts for no arguments, a zero budget and a missing module path. The native-build CTest runs the exact Rust-frontend-generated synthetic DLL/SO through the runner, parses the success JSON and asserts positive guest instruction accounting on Windows/MSVC and Linux/GCC. This remains synthetic integration evidence; no M4 retail-title boot is established. See ADR 0110.

## ISO-to-native preparation CLI (PR #61)

The Rust frontend adds `recompile-iso <disc.iso> <new-output-directory>`. It opens the raw ISO read-only, resolves `SYSTEM.CNF` BOOT2 (preferred) or BOOT, extracts only a validated MIPS `ET_EXEC`, and then calls the existing static recompiler with `--native-build` enabled automatically. The original ISO is bound as parent provenance and its SHA-256 is validated against the exact extracted boot ELF before generated C++ and the native project are written.

The new output directory contains `boot.elf`, the initial 32 MiB `boot.ram.bin`, `boot.cpp` plus any split parts, `boot.manifest.json`, and `boot.native/` with the CMake project, host probe and RG REPS2 ENGINE module wrapper. Existing output directories are refused. If extraction or recompile fails after directory creation, the incomplete output directory is removed. The ISO itself is never modified.

This is an artifact-preparation pipeline, not a consumer-ready one-click Play workflow. The generated CMake project still requires a host CMake build and the installed runner, and guest execution remains bounded with explicit stop statuses. No retail-title boot or playability is claimed. An integration test uses a synthetic PS2 ISO to verify exact boot extraction, original ISO immutability, parent ISO digest binding, generated artefacts, no-overwrite behavior and cleanup on invalid ISO input.

## Platform priority

Active release-validation target: **Windows x64 desktop**. The maintained CI path must verify MSVC build, DLL loading, the installed runner, Unicode filesystem paths, public SDK headers and static-library consumption. Linux remains a portability regression target.

Android and macOS are deferred. An earlier exploratory Android NDK cross-build was not retained as a maintained release gate; it demonstrated cross-compilation only and does not imply device/runtime, APK, JNI, or gameplay support.

## Windows x64 install and deployment gate

The current hardening increment adds a fresh install-tree CI test. It stages the frontend, engine runner, static library, public headers and MSVC runtime; runs from outside the build tree; loads a generated DLL copied into a Unicode path with spaces; verifies its UTF-8 JSON path round-trip and guest instruction count; then compiles/runs a second C++ consumer using only the installed public headers and static library. The portable SDK ZIP is now also generated by the same gate with a versioned bundle manifest and UTF-8 per-file SHA-256 list; CI extracts it into another Unicode path, verifies all payloads and repeats runner plus consumer tests against the relocated package. The bundle is uploaded as a short-lived CI artifact only when the smoke gate passes. Validated on Windows/MSVC in CI run 42693161421 for PR head `846f8e1`: the install tree, Unicode DLL load/path round-trip, external C++ consumer, portable ZIP creation, licence/UTF-8-LF checksums, Unicode relocation with the install tree removed from `PATH`, relocated runner execution and extracted-SDK C++ consumer all passed. The `rg-reps2-engine-windows-x64-msvc` artifact (artifact ID `13707637812`, 1,355,852 bytes, 14-day retention) records `source_commit = 846f8e1`, `engine_abi_version = 2` and `runtime_abi_version = 1`; its sidecar verifies, and `sha256sum -c` accepts the manifest. Linux/GCC and Rust MSRV 1.88 also passed on the same commit. This is a developer SDK bundle, not a polished game launcher or signed installer, and the bundled module is a synthetic non-playable fixture.

## M4 real-title evidence

Current target identity recorded by the local investigation:

- Game: Downhill Domination USA, serial SCUS_971.77.
- Boot ELF SHA-256: `adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c`.
- ISO SHA-256: `0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86`.
- Entry PC: `0x0010A008`.
- Both explicitly declared copied-code regions are required by the current trace:
  - `0x80076000 <- 0x002829F8 (0x740 bytes)`
  - `0x80075000 <- 0x002831A0 (0x330 bytes)`
- Translation coverage (build manifest v3 on the current revision): 60,466 basic blocks, 314,732 guest instructions, 7,146 accepted roots (9 skipped roots, 24 skipped blocks), 98,419 static edges, 623 unresolved indirect targets, 664 unsupported-instruction block stops, 255 guest exceptions, 9 translation units and a 32 MiB initial RAM image. The per-title record is kept in `docs/COMPATIBILITY_MATRIX.md`.

**Latest PR #82 frontier and correction (ADR 0129):** the bounded Windows/MSVC probe on PR head `d4496a7` stopped at `pc=0x800761A4` after 3,890,274 instructions on `lw $20, 0($2)`. The run report records `$2 = 0xFFFFFFFFB0001800`, so the effective guest address is the KSEG1 alias of physical `0x10001800`. PS2SDK identifies this as EE Timer 3 Count (`T3_COUNT`), not a DMAC register. REPS2 already models only `T3_MODE` (ADR 0073); `T3_COUNT`/`T3_COMP`, counting, compare matching and interrupt delivery remain unsupported. This observation is from the PR #82 head and is not a fresh real-title probe on merged main `431f71d`.

**Historical probe evidence (before PR #48; not rerun at PR #48 merge):** an earlier bounded probe stopped as `budget_exhausted` at PC `0x0025AAF8` with `203,526` `sceSifGetReg` calls, parked in the `SMFLAG` wait loop (`0x0025C328`..`0x0025C334`). That remains historical evidence only.

**Historical probe evidence (this revision, `046e063` + ADR 0099):** the probe was regenerated on the exact baseline from the extracted boot ELF with `--discover --tu-count 8 --native-build --parent-iso "<owned ISO>" --probe-budget 5000000 --hle-syscalls` and both relocated regions, then built with MSVC 19.51 Release and executed. It ran `4,999,998` guest instructions and stopped as `budget_exhausted` at PC `0x0025C108`, calling `SifSetDma` (`0x77`) at `0x0025AAA4`. The SIF observations were `sif_iop=resident`, `sif_smflag=0x00020000`, `sif_msflag=0x00020000`, `sif_hw_subaddr=0`, `sif_get_regs=4`, `sif_set_regs=2`, `sif_set_dma=2`, `sif_cmd_pkts=2`, `sif_cmd_cid=0x80000002`, `sif_cmd_init=1`. The `SMFLAG` wait loop is gone: the title passes `sceSifInitCmd`, adopts the (zero) sub-address, publishes `SYSREG_SUBADDR`/`MAINADDR`, sends `INIT_CMD`, and enters the SIF RPC wait loop at `0x0025C928` (which calls `0x0025C108`, indexing the global table at `0x00761C80`, and loops while the entry is zero). This is bounded-execution evidence of forward progress, not a boot stage or Playable evidence.

**Historical probe evidence (this revision + ADR 0100):** with the modelled inbound SIF0 RPCINIT reply, the probe executed `3,169,504` guest instructions (below the `5,000,000` budget) and stopped as `unhandled_syscall` at PC `0x0025A724` (`hle_number=0x44`, `$a0=3`), calling `WaitSema` (`0x44`) inside `sceSifBindRpc`, with `syscalls=70` (`52` handled). The SIF observations were `sif_iop=resident`, `sif_smflag=0x00020000`, `sif_msflag=0x00020000`, `sif_get_regs=4`, `sif_set_regs=3`, `sif_set_dma=3`, `sif_cmd_pkts=3`, `sif_cmd_cid=0x80000009` (`SIF_CMD_RPC_BIND`), `sif_cmd_init=1`, `sif_ee_inbound=1`, `sif_ee_inbound_cid=0x80000001` (`SIF_CMD_SET_SREG`), `sif_ee_sreg0=1`, `sif_rpcinit_sent=1`, `sif_ee_sif0_event=1`. The `0x0025C928` getter loop is gone: the title passes `sceSifInitRpc` (its `sregs[0]` became 1 through the modelled reply, not a hardcoded write), calls `sceSifBindRpc`, sends `RPC_BIND` (`0x80000009`), and blocks on the bind-completion semaphore. This is bounded-execution evidence of forward progress, not a boot stage or Playable evidence.

**Historical probe evidence (this revision + ADR 0101):** with the EE semaphore signal/wait services modelled, the probe was regenerated on this revision with the same command and both relocated regions, then built with MSVC 19.51 Release and executed. It ran `3,169,504` guest instructions and stopped as `semaphore_wait_blocked` at PC `0x0025A724`, calling `WaitSema` (`0x44`) with `$a0 = 3`. The semaphore observations were `sema_calls=4`, `sema_live=4`, `sema_id=3`, `sema_init=0`, `sema_max=1`, `sema_wait=1`, `sema_op_id=3`, `sema_wait_count=0`, `sema_wait_blocked=1`. The stop changed from `unhandled_syscall` (unknown service `0x44`) to a precise, honest reason: the guest blocked on a zero-count semaphore (id 3, `init_count = 0`), which only the unmodelled IOP RPC bind reply would signal. The instruction count and PC are unchanged, confirming nothing was fabricated to pass the wait. This is bounded-execution evidence, not a boot stage or Playable evidence. (ADR 0101)

**Historical probe evidence (this revision + ADR 0104):** with the resident IOP SIFRPC no-server bind reply modelled, the probe was regenerated on this revision with the same command and both relocated regions, built with MSVC 19.51 Release and executed. At the `5,000,000` budget it ran `4,999,994` guest instructions and stopped as `budget_exhausted` at PC `0x002478F0`; at a `20,000,000` budget it ran `19,999,999` and stopped at the same PC. The bind observations were `sif_rpc_bind=1`, `sif_rpc_bind_sid=0x80000592`, `sif_rpc_bind_sema=3`, `sif_rpc_bind_signalled=1` (three replies by the larger budget). The `WaitSema` on id 3 consumed the raised count (`sema_wait_blocked=0`), the following `DeleteSema(3)` was serviced, and the title continued on its own failed-bind error path. The stop is now budget exhaustion, which is a weaker evidence class than a semantic boundary: it proves the previously blocking bind wait completes and the guest keeps executing, and it proves nothing about a boot stage, a menu, or Playable.

**Historical M4 frontier (before the later CDVD server and CFG increments):** the next evidenced requirement is the IOP module behind the observed sid `0x80000592`: server state, `SIF_CMD_RPC_CALL` replies, and transfer payloads. Target disassembly shows the title loops on `sceSifBindRpc(cd, 0x80000592, 0)` and tests `cd->server`, delaying ~65,536 iterations between retries, so a larger budget cannot advance it; the other bind sites use `0x00123456`/`0x00123457`, `0x80000100`/`0x80000101`, `0x80000400`, `0x80000597` and `0x80000901`..`0x80000905` (`docs/RESEARCH.md`). The bind path now completes with the documented no-server reply (ADR 0104), so the guest's own error handling runs instead of a fabricated success; without that module no bind can succeed and no RPC call can be answered. Modelling it requires the M5 IOP subsystem. The EE DMAC interrupt is a separate COP0 line (IP3), not an INTC source, so SIF0 completion must not be wired into INTC; delivering it needs the `AddDmacHandler` chain and an EE interrupt-exception path (`docs/RESEARCH.md`). ADR 0103 adds the fail-closed `I_STAT`/`I_MASK` register file that such a path would consume. This is not a full IOP implementation and does not deliver the RPC call path.

**Current M4 frontier (PRs #79–#82; corrections ADRs 0129–0130):** PR #79 registers NCmd (`0x80000595`) and SearchFile (`0x80000597`) and answers only the evidenced NCmdDiskReady function 14; other NCmd and SearchFile calls remain recorded and refused. PRs #80 and #81 fixed the observed delay-slot-entry CFG edge and relocated-code function discovery, advancing the Downhill probe into the copied kernel region. PR #82 added the 32-lane GPR snapshot; its Windows/MSVC probe on head `d4496a7` stops at `0x800761A4` on `lw $20, 0($2)`, with `$2 = 0xFFFFFFFFB0001800`. This is EE Timer 3 Count (`T3_COUNT`, physical `0x10001800`), not a DMAC register. The existing ADR 0073 model covers only `T3_MODE`; Timer 3 counting, `T3_COMP`, compare/overflow behavior and timer interrupts remain unimplemented. The MMIO dispatch now normalizes direct-mapped KSEG0/KSEG1 aliases before matching modeled physical registers (ADR 0130); this does not add Count/Compare support. SearchFile/ISO9660 file lookup, SIF transfer/interrupt delivery, a scheduler/IPC producer, unsupported instructions and unresolved indirect edges remain blockers. The PR #82 game probe is not a fresh probe on merged main `431f71d`; no guest-visible boot stage is established.

The D_STAT read no longer refuses because the HLE boot environment seeds the kernel-initialized idle DMAC state (ADR 0091); `0x78` starts the SIF0 channel through the DMAC owner, publishing the SIF0 completion event (ADR 0092); `0x12` records the channel-5 handler in a bounded host table with an HLE-local id (ADR 0095); `0x16` sets the channel's `D_STAT` interrupt-mask bit only when it is clear, without writing `$v0` (ADR 0096); and `0x7a` serves the SIF register file (ADR 0097). Remaining unknowns include the true hardware initial DMAC state, ROM kernel command buffer/tag words, kernel handler-id values and chain order, `EnableDmac` return value, retail `SIF_SYSREG_SUBADDR`, IOP `SMFLAG` convention and initialization sequence, whether additional IOP/SIF side effects are required, and DMAC/SIF1 interrupt delivery. The `0x79`/`0x77` EE service models exist after PR #48, but have not been validated by a new real-title probe and do not establish the complete IOP handshake.

## Implemented M4 path on the baseline

The merged M4 path includes:
- EE syscall dispatch through the guest-visible service table and translated guest syscall handlers.
- Observed early kernel HLE calls: SetupThread, SetupHeap, CreateSema, SetSyscall, GetEntryAddress, FlushCache, SifSetDChain (`0x78`, SIF0 channel start model), AddDmacHandler (`0x12`, channel-5 record), EnableDmac (`0x16`, D_STAT mask model), and SifGetReg (`0x7a`). ADR 0098 adds SifSetReg (`0x79`), SifSetDma (`0x77`) and a bounded SIFCMD command model; ADR 0099 adds the modeled resident IOP `SIFCMD` startup that publishes `SMFLAG` CMDINIT; ADR 0100 adds the inbound IOP-to-EE SIF0 RPCINIT reply. ADR 0101 adds the EE semaphore `DeleteSema`/`SignalSema`/`iSignalSema`/`WaitSema` services and the honest `semaphore_wait_blocked` stop. The probe now passes the SIF RPC wait loop at `0x0025C928` and stops at `WaitSema` at `0x0025A724` on a zero-count semaphore, waiting for the RPC bind reply. The model is not a full IOP, does not deliver DMAC/SIF1/RPC interrupts, has no scheduler or IPC producer, and does not establish a completed RPC handshake. No gameplay or boot stage is evidenced.
- The bounded InitAlarm patch path and explicitly declared relocated code.
- CreateThread dormant records, StartThread READY queue bookkeeping, boot-thread id and priority changes. This is not a context-switching scheduler.
- Host-session GetOsdConfigParam/SetOsdConfigParam behavior. This is not a full physical OSD/board configuration model.
- VU0 LQC2/SQC2 quadword data movement with a separate VF file and architectural VF0 constant. This is not VU arithmetic or general VU execution.

See `docs/ROADMAP.md`, `docs/CAPABILITIES.md`, `docs/COMPATIBILITY_MATRIX.md`, and the relevant ADRs for exact scope and validation.

## Integrated D_STAT / DMAC register model

PR #22 introduced the D_STAT contract; ADR 0091 extends it. `runtime/include/reps2/runtime/ee_dmac.hpp` owns DPCR/D_STAT/D_PCR and the SIF0 channel-5 CHCR/MADR/QWC/TADR registers. A default register state is unknown; every read/write fails closed until a state is established. Production runtime does not assume a hardware reset value.

`initialize_hle_boot_environment` seeds the kernel-initialized idle DMAC state (all modelled registers known and zero) for a host that opts into the HLE layer. This is a documented model of the state the replaced EE kernel would present, derived from PS2SDK's own `dma_reset` idle semantics and cross-checked against PCSX2, not a measured hardware reset value. A SIF0 `CHCR` `STR` start is modelled as an immediately completed transfer that publishes the D_STAT SIF0 event and self-clears `STR`; there is no IOP backpressure or timing.

A generated-code CTest fixture (`reps2_dmac_dstat_codegen_smoke`) exercises fail-closed behavior for unknown state, seeded read/write for every modelled register, the `CHCR5` start → D_STAT SIF0 event → W1C clear sequence, and the HLE boot environment. It is not a real-title probe and does not prove SIF0 pending state during the target trace. The follow-up M4 requirement is the complete IOP/SIF handshake and interrupt-driven command dispatch; the EE-side `0x77`/`0x78` services exist, but a new real-title probe after PR #48 is required to establish how this path behaves with those changes.

## EE INTC register model

`runtime/include/reps2/runtime/ee_intc.hpp` owns the EE interrupt controller's
`I_STAT` (`0x1000F000`, write-one-to-clear) and `I_MASK` (`0x1000F010`,
write-one-to-toggle over the low 16 bits) registers and is wired into the
`EeMemory` MMIO dispatch table. Every access fails closed until seeded;
`initialize_hle_boot_environment` seeds the idle state (no pending source, all
sources masked) as an explicitly documented HLE model. `raise_source()` records
pending state and `line_asserted()` reports `(I_STAT & I_MASK) != 0`; **no CPU
exception is delivered** and no source-to-CP0-pin or DMAC/SIF0 routing is
claimed. A generated-code CTest fixture (`reps2_intc_codegen_smoke`) covers the
fail-closed and seeded paths. See ADR 0103.

## IOP SIF RPC bind reply

`process_sif_command` answers an EE `SIF_CMD_RPC_BIND` (`0x80000009`) as the
resident IOP SIFRPC module does when `_search_svdata` finds no server: an
`SIF_CMD_RPC_END` (`0x80000008`) reply carrying `sd = buf = cbuf = NULL` for the
request's `cd`/`pkt_addr`/`rpc_id`. The reply is written into the guest-published
EE receive buffer, publishes the SIF0 `D_STAT` event, and
`process_ee_sif_command` performs the EE-side `_request_end` effects: write the
client's `server`/`buf`/`cbuf`, raise the client semaphore through the shared
`raise_ee_sema_count` rule, and free the request packet. A bind therefore fails
exactly as it would on an IOP without that module, and the title follows its own
error path; `SIF_CMD_RPC_CALL` replies, IOP server payloads and EE interrupt
delivery remain unimplemented. The probe reports `sif_rpc_bind`,
`sif_rpc_bind_sid`, `sif_rpc_bind_sema` and `sif_rpc_bind_signalled`. See
ADR 0104.

## EE DMAC register naming and interrupt condition

`EeDmacState` now carries the SDK register names: `ctrl` for `D_CTRL`
(`0x1000E000`, bit 0 `DMAE`) and `pcr` for `D_PCR` (`0x1000E020`, channel
priority), replacing the earlier `dpcr`/`dicr` labels; `D_STAT` is unchanged. The
free function `ee_dmac_interrupt_pending(d_stat, dmae, controller_held)`
implements the documented condition — a `D_STAT` low/high half-word bit match or
the bus-error status bit, only while the master enable is set and the controller
is not held. It delivers no exception: the DMAC reports on its own COP0 line
(IP3), separate from the INTC (IP2), and `INTC_SBUS` is IOP-side only. See
ADR 0105 and the research note in `docs/RESEARCH.md`.

## Host media boundary

`HostMediaSource` (`host_media.hpp`) is the single place disc data can enter the
runtime: a host-installed, read-only callback serving whole 2048-byte user
sectors, held as `EeState::media` and absent by default. A zero-sector read is a
successful no-op; non-empty reads require a span large enough for all sectors,
refuse an absent source or a range crossing the 32-bit LBN space before the host
is called, and require the host adapter to validate its image length. A device
model that needs disc data must fail closed while the source is absent. The
runtime never opens files. See ADR 0106. `EngineSession` attaches the host source
before it calls the generated module initializer, so boot-time module code can
observe the same read-only source; the initializer resets HLE and observation
state but never the media attachment. `rg_reps2_engine_smoke` covers that
ordering with a module whose initializer reads one synthetic sector.

## REA investigation handoff

`docs/REA_WORKFLOW.md` describes the local Windows workflow and current provider constraints. REA is a static reverse-engineering workbench, not a PS2 emulator. Its documented Windows Ghidra P0 path does not admit MIPS; its generic MIPS Ghidra path is documented for Linux/macOS and restricts ELF/profile declarations. A PS2 R5900 machine-specific ELF may therefore be refused. Do not modify its `e_flags` to bypass admission, and do not assume WSL removes target-format restrictions.

Keep the ISO, extracted ELF, BIOS/firmware, REA Evidence bundles and full decompiler output outside Git. Commit only lawful, compact, independently written observations, digests, public references and explicit unknowns.

## Local PS2 EE toolchain

A project-local PS2 EE toolchain is installed at `D:\ps2dev\v2.0.0\ps2dev` from the official ps2dev `v2.0.0` Windows release (archive SHA-256 recorded in `D:\ps2dev\v2.0.0\VERIFICATION.md`). Verified: `objdump`, `readelf`, `as` and `ld` (GNU Binutils 2.45.1) for target `mips64r5900el-ps2-elf`; `gcc` is **not** verified because its `libiconv-2.dll`/`libwinpthread-1.dll` dependencies are absent. The toolchain is used only to inspect/assemble synthetic fixtures and to disassemble SDK/guest code outside Git; no game material is committed.

## Remaining M4 exit criteria

The remaining work is evidence-led and should be pursued in this order:

1. **Resolve the current EE Timer 3 access.** The latest PR #82 probe reaches `T3_COUNT` at physical `0x10001800` from relocated code. Capture the target's `T3_MODE`/`T3_COMP` configuration, timer writes, enable state, and counter consumer before implementing count progression. The partial mode-only behavior from ADR 0073 remains the limit; do not synthesize a zero count or host-clock value.
2. **Complete the observed CDVD/NCMD request path.** NCmdDiskReady (NCMD function 14) is served, but remaining NCmd functions and SearchFile server calls are still refused. Trace the exact request payloads and implement only the ISO-backed path lookup/read behavior proven by the owned disc. The host-media boundary remains read-only.
3. **Complete shared event delivery.** Model required SIF0/SIF1 DMA completion, DMAC interrupt IP3, EE INTC/exception routing, and the event producer/scheduler needed to release blocked guest work. Keep IOP and EE interrupt lines distinct and fail closed until their routing is established.
4. **Reduce frontend/runtime execution gaps.** Address the recorded 664 unsupported-instruction block stops and 623 unresolved indirect targets with instruction-level evidence and regression tests, not title-specific code patches.
5. **Capture a repeatable guest-visible boot stage.** Re-run on exact merged main, exact ISO/ELF digests, Windows x64/MSVC, recorded engine/runtime configuration and repeatable trace. A bounded host run or instruction-count advance is not a boot stage; Playable remains a later, separate gate.

M4 completion is one evidenced real-title stage, not proof of playability or multi-game compatibility. Later roadmap phases must expand shared EE/IOP, VU, GS, CDVD, SPU2, controller, memory-card and launcher subsystems before broad compatibility or a release claim.
