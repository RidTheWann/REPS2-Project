// SPDX-License-Identifier: GPL-3.0-or-later
// REPS2 prototype sample data.
// Real documented titles (SCUS_971.77, SLUS_213.76) carry their TRUE backend
// status from docs/COMPATIBILITY_MATRIX.md. All other entries are clearly
// synthetic DEMONSTRATION placeholders used only to exercise every UI state.
// No copyrighted cover art is included: covers are generated gradients.

window.REPS2_DATA = {
  lifecycle: [
    { id: 'imported',   label: 'Imported',     token: 'neutral',  icon: 'box',     req: 'Disc and boot ELF validated and stored.' },
    { id: 'analyzed',   label: 'Analyzed',     token: 'info',     icon: 'scan',    req: 'Analysis coverage recorded for the exact boot ELF digest.' },
    { id: 'translated', label: 'Translated',   token: 'progress', icon: 'code',    req: 'A documented subset emitted typed IR and C++ output.' },
    { id: 'native-built', label: 'Native-built', token: 'built',  icon: 'cpu',     req: 'Generated code compiled and linked against a specific runtime ABI.' },
    { id: 'boot-tested', label: 'Boot-tested', token: 'test',     icon: 'power',   req: 'Repeatable guest-visible boot stage reached and recorded.' },
    { id: 'playable',   label: 'Playable',     token: 'playable', icon: 'play',    req: 'A representative gameplay acceptance plan passes.' }
  ],
  failureStates: [
    { id: 'import-failed',       label: 'Import failed',         token: 'danger',  icon: 'alert' },
    { id: 'analysis-failed',     label: 'Analysis failed',       token: 'danger',  icon: 'alert' },
    { id: 'translation-incomplete', label: 'Translation incomplete', token: 'warn', icon: 'alert' },
    { id: 'native-build-failed', label: 'Native build failed',   token: 'danger',  icon: 'alert' },
    { id: 'execution-blocked',   label: 'Execution blocked',     token: 'blocked', icon: 'lock' },
    { id: 'unsupported',         label: 'Unsupported',           token: 'blocked', icon: 'lock' },
    { id: 'recovery-required',   label: 'Recovery required',     token: 'warn',    icon: 'refresh' }
  ],
  // Frontend pipeline stage ids mirror rust/frontend/src/main.rs (run_pipeline_stage).
  // Host CMake compile + module validation happen after the frontend stage set.
  pipelineStages: [
    { id: 'input.read',              label: 'Input read',         desc: 'Bounded, streamed read of the disc image' },
    { id: 'iso.extract_boot_elf',    label: 'Boot ELF discovery', desc: 'SYSTEM.CNF BOOT2 → BOOT; ELF32 LE EM_MIPS ET_EXEC' },
    { id: 'elf.parse',               label: 'ELF parsing',        desc: 'Program/section tables, symbols, relocations' },
    { id: 'elf.target_validation',   label: 'Target validation',  desc: 'Static EE-mapped PT_LOAD, no unapplied relocations' },
    { id: 'ee.ram_image',            label: 'EE RAM image',       desc: 'PT_LOAD materialization + BSS zero-fill (32 MiB)' },
    { id: 'cfg.discover',            label: 'CFG discovery',      desc: 'Reachability-guided block discovery' },
    { id: 'ir.lift',                 label: 'IR lifting',         desc: 'Typed IR + verification' },
    { id: 'codegen.emit_cpp',        label: 'Code generation',    desc: 'Deterministic C++20 translation units' },
    { id: 'artifact.write',          label: 'Artifact writes',    desc: 'Manifest, RAM image, generated sources' },
    { id: 'native.project.generate', label: 'Native project',     desc: 'CMake project + probe + module wrapper' }
  ],
  jobs: [
    { id: 'job-4021', game: 'Downhill Domination USA', stage: 'native.project.generate', status: 'succeeded', started: '14:02', dur: '149s', note: '9 translation units linked' },
    { id: 'job-4022', game: 'Sample Title C',          stage: 'artifact.write', status: 'running', started: '14:07', dur: '—', note: 'writing part 06/08' },
    { id: 'job-4019', game: 'Sample Title D',          stage: 'codegen.emit_cpp', status: 'failed', started: '13:41', dur: '12s', note: 'generated part failed host compile' },
    { id: 'job-4018', game: 'Black USA',               stage: 'cfg.discover', status: 'succeeded', started: '13:20', dur: '58s', note: '101,956 blocks' }
  ],
  games: [
    {
      id: 'g-downhill', title: 'Downhill Domination (USA)', serial: 'SCUS_971.77', region: 'NTSC-U',
      state: 'native-built', synthetic: false, glyph: 'DD', hue: 335,
      path: 'D:\\List Games\\Downhill Domination USA\\Downhill Domination (USA).iso',
      iso: { sha: '0fe2b77713d3440cc3030741ea0b18f49e3feaa09671a30a421e0d4888a7eb86', sizeGiB: 1.62 },
      elf: { sha: 'adfda7b73a8f05fb20a3f0f318772e9d3797fd4d6c0a6c0078ae392df0f0cf0c', entry: '0x0010A008' },
      analysis: { blocks: 60466, instructions: 314732, roots: 7146, unresolved: 623, unsupported: 664, tus: 9 },
      identity: { abi: '2', engine: '0.2.0', config: 'Release', compiler: 'MSVC 19.51.36260.0', cacheKey: 'a1f9c3…7b2e', ramMiB: 32 },
      // Latest EXECUTED probe on origin/main (ADR 0119 / PR #68, from 6756d0e; PR #69
      // corrected the PollSema return value and its exact-title re-run is pending).
      // The earlier semaphore_wait_blocked @ 0x0025A724 (ADR 0101) is historical.
      run: {
        status: 'budget_exhausted', executed: 4999998, pc: '0x00246DAC', budget: 5000000,
        cause: null, epc: null, cop0Status: null, badvaddr: null,
        syscall: '0x45 PollSema (poll loop)',
        note: 'Spins in a PollSema poll loop on semaphore id 5 (poll_sema=26479, poll_sema_acquired=0, poll_sema_count_before=0, sema_op_id=5; syscalls=26591, hle_handled=26574). sif_rpc_bind=2, sif_rpc_call=2 — no new SIF RPC traffic, so the awaited producer is an event REPS2 does not model yet. COP0 fields are not reported by this bounded probe.'
      },
      limitations: [
        'PollSema (0x45) poll loop on semaphore id 5 awaits a producer event the runtime does not model; no scheduler/IPC producer exists.',
        'Exact-title probe re-run after the PR #69 PollSema return-value correction is still pending (ROADMAP: re-run before naming the next frontier).',
        'SIF0/SIF1 DMA-completion + EE INTC interrupt chain not modelled; IOP-side RPC server (sid 0x80000592) unimplemented.',
        '623 unresolved indirect edges and 664 unsupported-instruction stops inside the translated image.'
      ],
      evidence: 'docs/COMPATIBILITY_MATRIX.md; docs/PROJECT_STATE.md (2026-11-10); ADRs 0091–0119 — latest executed probe ADR 0119 / PR #68. Bounded execution only — not a boot stage.'
    },
    {
      id: 'g-black', title: 'Black (USA)', serial: 'SLUS_213.76', region: 'NTSC-U',
      state: 'analyzed', synthetic: false, glyph: 'BK', hue: 210,
      path: 'D:\\List Games\\Black USA\\Black (USA).iso',
      iso: { sha: '65768cade88bfbaf566b67a3ef7a0bda5a9669779833914ffe7cd73a3e45cccd', sizeGiB: 3.94 },
      elf: { sha: '141b05209570458af9c8e27ab1911f258da6353909d813cd572df026254ffe4c', entry: '0x00100008' },
      analysis: { blocks: 101956, instructions: 504661, roots: 0, unresolved: 0, unsupported: 0, tus: 0 },
      identity: null,
      run: { status: 'not_tested', executed: 290344, pc: '0x0036D664', note: 'Historical probe only; NOT TESTED against the current runtime.' },
      limitations: ['FindAddress (0x83) ABI/side effects unresolved.', 'Status after later syscall + shared-runtime changes is NOT TESTED.'],
      evidence: 'docs/COMPATIBILITY_MATRIX.md; probe output 2026-11-05, ADR 0068 — historical only, NOT TESTED against the current runtime.'
    },
    {
      id: 'g-sca', title: 'Sample Title — Alpine Drift', serial: 'SLUS_900.01', region: 'NTSC-U',
      state: 'translated', synthetic: true, glyph: 'AD', hue: 150,
      path: 'C:\\Users\\Public\\Samples\\alpine-drift.iso',
      iso: { sha: '1111111111111111111111111111111111111111111111111111111111111111', sizeGiB: 2.10 },
      elf: { sha: '2222222222222222222222222222222222222222222222222222222222222222', entry: '0x00100000' },
      analysis: { blocks: 48210, instructions: 268004, roots: 5120, unresolved: 1290, unsupported: 4210, tus: 0 },
      identity: null, run: { status: 'not_run' },
      limitations: ['Translation incomplete: 4,210 unsupported and 1,290 unresolved edges.', 'Native build not started.'],
      evidence: 'Synthetic demonstration record.'
    },
    {
      id: 'g-scb', title: 'Sample Title — Nebula Rally', serial: 'SLUS_900.02', region: 'NTSC-U',
      state: 'imported', synthetic: true, glyph: 'NR', hue: 265,
      path: 'C:\\Users\\Public\\Samples\\nebula-rally.iso',
      iso: { sha: '3333333333333333333333333333333333333333333333333333333333333333', sizeGiB: 1.05 },
      elf: { sha: '4444444444444444444444444444444444444444444444444444444444444444', entry: '0x00100008' },
      analysis: null, identity: null, run: { status: 'not_run' },
      limitations: ['Analysis not started.'],
      evidence: 'Synthetic demonstration record.'
    },
    {
      id: 'g-scc', title: 'Sample Title — Harbor Circuit', serial: 'SLES_900.03', region: 'PAL',
      state: 'playable', synthetic: true, glyph: 'HC', hue: 25,
      path: 'C:\\Users\\Public\\Samples\\harbor-circuit.iso',
      iso: { sha: '5555555555555555555555555555555555555555555555555555555555555555', sizeGiB: 1.80 },
      elf: { sha: '6666666666666666666666666666666666666666666666666666666666666666', entry: '0x00100000' },
      analysis: { blocks: 39850, instructions: 210450, roots: 4310, unresolved: 12, unsupported: 88, tus: 6 },
      identity: { abi: '2', engine: '0.2.0', config: 'Release', compiler: 'MSVC 19.51', cacheKey: 'c4d8e0…91aa', ramMiB: 32 },
      run: { status: 'session_ok', executed: 8123044, pc: '0x00100008', budget: 20000000, gameplayPlan: 'GP-HC-01', note: 'DEMONSTRATION ONLY — Play is enabled solely to show the Playable presentation.' },
      limitations: ['Demonstration placeholder. Does not represent a real validated title.'],
      evidence: 'Synthetic demonstration record. NOT real compatibility evidence.'
    },
    {
      id: 'g-scd', title: 'Sample Title — Iron Vale', serial: 'SCUS_900.04', region: 'NTSC-U',
      state: 'native-build-failed', synthetic: true, glyph: 'IV', hue: 0,
      path: 'C:\\Users\\Public\\Samples\\iron-vale.iso',
      iso: { sha: '7777777777777777777777777777777777777777777777777777777777777777', sizeGiB: 2.60 },
      elf: { sha: '8888888888888888888888888888888888888888888888888888888888888888', entry: '0x00100008' },
      analysis: { blocks: 55123, instructions: 301770, roots: 6402, unresolved: 210, unsupported: 1502, tus: 7 },
      identity: null, run: { status: 'not_run' },
      limitations: ['Link failed: unresolved external symbol in generated part 04.', 'Compiler diagnostics retained in the build log.'],
      evidence: 'Synthetic demonstration record.',
      error: { stage: 'native.compile', code: 'REPS2_COMMAND_FAILED', detail: 'link.exe : fatal error LNK1120: 1 unresolved externals' }
    },
    {
      id: 'g-sce', title: 'Sample Title — Frostline', serial: 'SLES_900.05', region: 'PAL',
      state: 'recovery-required', synthetic: true, glyph: 'FL', hue: 190,
      path: 'E:\\Games\\frostline.iso',
      iso: { sha: '9999999999999999999999999999999999999999999999999999999999999999', sizeGiB: 1.44 },
      elf: { sha: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', entry: '0x00100000' },
      analysis: { blocks: 30012, instructions: 162040, roots: 3010, unresolved: 40, unsupported: 300, tus: 0 },
      identity: null, run: { status: 'not_run' },
      limitations: ['Referenced source ISO path is no longer present (drive not mounted).'],
      evidence: 'Synthetic demonstration record.',
      recovery: { reason: 'Source ISO missing at the recorded canonical path.', expectedSha: '9999…9999' }
    }
  ],
  sessions: [
    { id: 'session-1790000123456-4821-0', command: 'reps2 add-game', outcome: 'succeeded', events: 6, diagnostics: 0, started: '14:02:11' },
    { id: 'session-1790000987654-1290-0', command: 'reps2 recompile-elf', outcome: 'succeeded', events: 42, diagnostics: 3, started: '14:03:40' },
    { id: 'session-1790001555123-7741-0', command: 'reps2 add-game', outcome: 'failed', events: 9, diagnostics: 4, started: '13:20:03' }
  ]
};
