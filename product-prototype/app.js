// SPDX-License-Identifier: GPL-3.0-or-later
// REPS2 interactive prototype — application logic.
// This is a DESIGN VALIDATION artifact. Simulated actions are labelled as
// prototype demonstrations; nothing here imports an ISO or executes a PS2 game.

(function () {
  'use strict';
  const D = window.REPS2_DATA;
  const $ = (sel, root = document) => root.querySelector(sel);

  // ---------------------------------------------------------------- icons
  const ICONS = {
    box: '<path d="M21 8 12 3 3 8v8l9 5 9-5V8Z"/><path d="M3 8l9 5 9-5"/><path d="M12 13v8"/>',
    scan: '<path d="M3 7V5a2 2 0 0 1 2-2h2"/><path d="M17 3h2a2 2 0 0 1 2 2v2"/><path d="M21 17v2a2 2 0 0 1-2 2h-2"/><path d="M7 21H5a2 2 0 0 1-2-2v-2"/><path d="M3 12h18"/>',
    code: '<path d="m16 18 6-6-6-6"/><path d="m8 6-6 6 6 6"/>',
    cpu: '<rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6"/><path d="M9 1v3M15 1v3M9 20v3M15 20v3M20 9h3M20 15h3M1 9h3M1 15h3"/>',
    power: '<path d="M12 3v9"/><path d="M6.5 6.5a8 8 0 1 0 11 0"/>',
    play: '<path d="m6 4 14 8-14 8V4Z"/>',
    alert: '<path d="M12 9v4"/><path d="M12 17h.01"/><path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0Z"/>',
    lock: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>',
    refresh: '<path d="M3 12a9 9 0 0 1 15-6.7L21 8"/><path d="M21 3v5h-5"/><path d="M21 12a9 9 0 0 1-15 6.7L3 16"/><path d="M3 21v-5h5"/>',
    library: '<path d="M3 4h6v16H3zM13 4h6v16h-6z"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    list: '<path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/>',
    grid: '<rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/>',
    terminal: '<path d="m4 17 6-6-6-6"/><path d="M12 19h8"/>',
    settings: '<path d="M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6Z"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-2.9 1.2 2 2 0 1 1-4 0 1.7 1.7 0 0 0-2.9-1.2l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1A1.7 1.7 0 0 0 2 15a2 2 0 1 1 0-4 1.7 1.7 0 0 0 1.5-2.6l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1A1.7 1.7 0 0 0 9 4.6a2 2 0 1 1 4 0 1.7 1.7 0 0 0 2.7 1.5l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1A1.7 1.7 0 0 0 20 11a2 2 0 1 1 0 4Z"/>',
    search: '<circle cx="11" cy="11" r="7"/><path d="m21 21-4.3-4.3"/>',
    check: '<path d="M20 6 9 17l-5-5"/>',
    x: '<path d="M18 6 6 18M6 6l12 12"/>',
    chevron: '<path d="m9 18 6-6-6-6"/>',
    back: '<path d="M19 12H5M12 19l-7-7 7-7"/>',
    file: '<path d="M14 3v5h5"/><path d="M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8Z"/>',
    play2: '<circle cx="12" cy="12" r="9"/><path d="m10 8 6 4-6 4V8Z"/>',
    history: '<path d="M3 3v5h5"/><path d="M3.05 13A9 9 0 1 0 6 5.3L3 8"/><path d="M12 7v5l3 2"/>',
    layers: '<path d="m12 2 9 5-9 5-9-5 9-5Z"/><path d="m3 12 9 5 9-5"/><path d="m3 17 9 5 9-5"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 16v-4M12 8h.01"/>',
    copy: '<rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>',
    stop: '<rect x="6" y="6" width="12" height="12" rx="2"/>'
  };
  function icon(name, cls = 'icon') {
    return `<svg class="${cls}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICONS[name] || ICONS.info}</svg>`;
  }

  // ---------------------------------------------------------------- state
  const LIFECYCLE = Object.fromEntries(D.lifecycle.map((s) => [s.id, s]));
  const FAILURES = Object.fromEntries(D.failureStates.map((s) => [s.id, s]));
  function statusMeta(id) {
    return LIFECYCLE[id] || FAILURES[id] || { id, label: id, token: 'neutral', icon: 'info', req: '' };
  }
  function pill(id, extra = '') {
    const m = statusMeta(id);
    const label = m.label + (m.req ? ': ' + m.req : '');
    return `<span class="pill pill-${m.token}" role="status" aria-label="${escapeAttr(label)}" title="${escapeAttr(label)}">${icon(m.icon, 'icon')}<span>${m.label}</span></span>${extra}`;
  }
  function escapeAttr(s) { return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
  function escapeHtml(s) { return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
  function cover(g, cls = 'cover') {
    const h = g.hue != null ? g.hue : 220;
    return `<div class="${cls}" style="background:
        linear-gradient(150deg, hsl(${h} 45% 22%), hsl(${(h + 40) % 360} 40% 12%) 70%, var(--bg-deep));
        position:relative; overflow:hidden;">
        <span class="cover-tag">${g.synthetic ? 'Sample' : 'Owned'}</span>
        <span class="cover-glyph">${escapeHtml(g.glyph || 'RE')}</span>
      </div>`;
  }
  function mono(v) { return `<span class="mono">${escapeHtml(v)}</span>`; }

  const state = {
    route: 'library',
    params: {},
    viewMode: 'grid',
    query: '',
    filter: 'all',
    sort: 'recent',
    selected: 'g-downhill',
    railCollapsed: false,
    devMode: false,
    dialog: null,
    toasts: [],
    pipeline: null, // { gameId, index, running }
    settings: {
      libraryRoot: '%LOCALAPPDATA%\\REPS2\\Library',
      buildRoot: '%LOCALAPPDATA%\\REPS2\\Builds',
      logRoot: '%LOCALAPPDATA%\\REPS2\\Logs',
      hleSyscalls: true,
      instructionTrace: false,
      defaultBudget: 8000000,
      controller: true,
      reducedMotion: false
    }
  };

  // ---------------------------------------------------------------- helpers
  function visibleGames() {
    const q = state.query.trim().toLowerCase();
    let list = D.games.filter((g) => {
      const okQ = !q || g.title.toLowerCase().includes(q) || g.serial.toLowerCase().includes(q);
      const okF = state.filter === 'all' || g.state === state.filter;
      return okQ && okF;
    });
    const order = D.lifecycle.map((s) => s.id).concat(D.failureStates.map((s) => s.id));
    if (state.sort === 'title') list = list.slice().sort((a, b) => a.title.localeCompare(b.title));
    else if (state.sort === 'serial') list = list.slice().sort((a, b) => a.serial.localeCompare(b.serial));
    else if (state.sort === 'status') list = list.slice().sort((a, b) => order.indexOf(a.state) - order.indexOf(b.state));
    return list;
  }
  function gameById(id) { return D.games.find((g) => g.id === id); }

  function nextActions(g) {
    const a = [];
    const playable = g.state === 'playable';
    const canRun = ['native-built', 'boot-tested', 'playable'].includes(g.state);
    // Play is offered ONLY in the Playable state; earlier states offer their next
    // valid action instead (COMPATIBILITY_UX_MODEL.md §6).
    if (playable) a.push({ label: 'Play', icon: 'play', kind: 'btn-run', action: 'play' });
    if (canRun) a.push({ label: g.state === 'boot-tested' ? 'Run test session' : 'Run bounded session', icon: 'play2', kind: playable ? 'btn' : 'btn-run', action: 'run' });
    if (g.state === 'imported') a.push({ label: 'Analyze', icon: 'scan', kind: 'btn', action: 'analyze' });
    if (g.state === 'analyzed') a.push({ label: 'Translate', icon: 'code', kind: 'btn', action: 'translate' });
    if (g.state === 'translated') a.push({ label: 'Native build', icon: 'cpu', kind: 'btn', action: 'build' });
    if (g.state === 'native-build-failed') a.push({ label: 'Retry build', icon: 'refresh', kind: 'btn', action: 'build' });
    if (g.state === 'recovery-required') a.push({ label: 'Relink source ISO', icon: 'refresh', kind: 'btn', action: 'relink' });
    if (FAILURES[g.state]) a.push({ label: 'Export diagnostics', icon: 'file', kind: 'btn-ghost', action: 'export' });
    a.push({ label: 'Reveal in Explorer', icon: 'file', kind: 'btn-ghost', action: 'reveal' });
    return a;
  }
  function playNote(g) {
    return g.state === 'playable' ? '' :
      `<p class="muted" style="margin:12px 0 0;font-size:12px">${icon('info', 'icon-sm')} Play becomes available only when a title reaches <strong>Playable</strong>; this title is <strong>${statusMeta(g.state).label}</strong>.</p>`;
  }

  // ---------------------------------------------------------------- toasts / dialog
  function toast(msg, kind = 'info') {
    const id = 't' + Math.random().toString(36).slice(2, 8);
    state.toasts.push({ id, msg, kind });
    renderToasts();
    setTimeout(() => { state.toasts = state.toasts.filter((t) => t.id !== id); renderToasts(); }, 4200);
  }
  function renderToasts() {
    const host = $('#toasts');
    host.innerHTML = state.toasts.map((t) => `
      <div class="toast" role="status" aria-live="polite">
        ${icon(t.kind === 'error' ? 'alert' : 'info', 'icon')}
        <span>${escapeHtml(t.msg)}</span>
      </div>`).join('');
  }
  let dialogInvoker = null;
  function openDialog(dialog) { dialogInvoker = document.activeElement; state.dialog = dialog; renderDialog(); }
  function closeDialog() {
    state.dialog = null; $('#dialog-root').innerHTML = '';
    const target = dialogInvoker || $('#main');
    dialogInvoker = null;
    if (target && typeof target.focus === 'function') target.focus();
  }

  function renderDialog() {
    const root = $('#dialog-root');
    if (!state.dialog) { root.innerHTML = ''; return; }
    const d = state.dialog;
    const body = {
      addGame: () => `
        <div class="banner banner-demo">${icon('info')}<span><strong>Prototype demonstration.</strong> This dialog does not read or import any file. The real path is <span class="mono">reps2 add-game &lt;iso&gt;</span>.</span></div>
        <div class="field">
          <label for="dlg-path">Owned PS2 ISO path</label>
          <input class="input mono" id="dlg-path" value="D:\\List Games\\Sample Title\\game.iso" />
          <span class="hint">Raw 2048-byte-sector ISO 9660 only. BIN/CUE 2352, CHD, UDF and Joliet/Rock Ridge are not supported.</span>
        </div>
        <div class="dropzone">${icon('file', 'icon')}<div style="margin-top:8px">Drag a legally owned ISO here, or use the path above</div></div>
        <div class="field">
          <label for="dlg-lib">Library root</label>
          <input class="input mono" id="dlg-lib" value="${escapeAttr(state.settings.libraryRoot)}" />
        </div>`,
      confirm: () => `<p>${escapeHtml(d.message || '')}</p>`,
      relink: () => `
        <div class="banner banner-warn">${icon('refresh')}<span>Relinking must resolve to the <strong>same content digest</strong>. REPS2 never silently binds a record to a different disc.</span></div>
        <div class="field"><label for="dlg-relink">Expected ISO SHA-256</label><input class="input mono" id="dlg-relink" value="${escapeAttr(d.expectedSha || '')}" readonly /></div>
        <div class="field"><label for="dlg-newpath">New location of the same ISO</label><input class="input mono" id="dlg-newpath" value="E:\\Games\\frostline.iso" /><span class="hint">Digest is re-verified before the record is updated.</span></div>`,
      errorDetail: () => `
        <div class="banner banner-danger">${icon('alert')}<span>Command failure event <span class="mono">${escapeHtml(d.code)}</span> · scope <span class="mono">${escapeHtml(d.stage)}</span></span></div>
        <div class="console"><span class="ln-error">${escapeHtml(d.detail)}</span>
<span class="ln-ts">[session-1790000987654-1290-0]</span> <span class="ln-info">pipeline.stage.failed stage=${escapeHtml(d.stage)}</span></div>
        <p class="muted">Recovery: inspect the compiler diagnostics, fix or re-run; the UI reads this from the structured failure event, not from scraped text.</p>`
    }[d.kind]();
    const foot = {
      addGame: `<button class="btn" data-act="close-dialog">Cancel</button><button class="btn btn-primary" data-act="demo-import">Import (demo)</button>`,
      confirm: `<button class="btn" data-act="close-dialog">Cancel</button><button class="btn ${d.danger ? 'btn-danger' : 'btn-primary'}" data-act="${d.confirmAct || 'close-dialog'}">${escapeHtml(d.confirmLabel || 'Confirm')}</button>`,
      relink: `<button class="btn" data-act="close-dialog">Cancel</button><button class="btn btn-primary" data-act="demo-relink">Re-verify and relink (demo)</button>`,
      errorDetail: `<button class="btn" data-act="close-dialog">Close</button><button class="btn btn-primary" data-act="demo-copy">Copy event JSON</button>`
    }[d.kind];
    root.innerHTML = `
      <div class="scrim" data-act="scrim">
        <div class="dialog" role="dialog" aria-modal="true" aria-labelledby="dlg-title" tabindex="-1">
          <div class="dialog-head"><h2 id="dlg-title">${escapeHtml(d.title)}</h2><button class="btn btn-icon btn-ghost" data-act="close-dialog" aria-label="Close dialog">${icon('x')}</button></div>
          <div class="dialog-body">${body}</div>
          <div class="dialog-foot">${foot}</div>
        </div>
      </div>`;
    const dlg = $('.dialog', root);
    const first = dlg.querySelector('input, button');
    if (first) first.focus();
  }
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && state.dialog) { closeDialog(); }
    if (state.dialog && e.key === 'Tab') {
      const f = $('#dialog-root').querySelectorAll('a[href],button:not([disabled]),input,select,textarea,[tabindex]:not([tabindex="-1"])');
      if (!f.length) return;
      const first = f[0], last = f[f.length - 1];
      if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
      else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
    }
  });

  // ---------------------------------------------------------------- shell
  const RAIL = [
    { id: 'library', label: 'Library', icon: 'library' },
    { id: 'addgame', label: 'Add Game', icon: 'plus' },
    { id: 'queue', label: 'Build Queue', icon: 'layers' },
    { id: 'developer', label: 'Developer Center', icon: 'terminal' },
    { id: 'settings', label: 'Settings', icon: 'settings' }
  ];
  function renderShell(content) {
    const railItems = RAIL.map((r) => `<a href="#${r.id}" data-act="nav" data-route="${r.id}" aria-current="${state.route === r.id ? 'page' : 'false'}">${icon(r.icon)}<span>${r.label}</span></a>`);
    return `
    <a class="skip-link" href="#main">Skip to content</a>
    <div class="app ${state.railCollapsed ? 'rail-collapsed' : ''}">
      <header class="topbar">
        <div class="brand"><span class="brand-mark">R2</span><span>REPS2<small>RG REPS2 ENGINE</small></span></div>
        <label class="search">${icon('search', 'icon')}<span class="sr-only">Search library</span>
          <input id="global-search" type="search" placeholder="Search library by title or serial…" value="${escapeAttr(state.query)}" />
        </label>
        <div class="topbar-spacer"></div>
        <span class="kbd" title="Command palette">Ctrl K</span>
        <button class="btn btn-icon btn-ghost" data-act="toggle-rail" aria-label="Toggle navigation rail">${icon('list')}</button>
      </header>
      <nav class="rail" aria-label="Primary">
        <div class="rail-label">Library</div>
        ${railItems.slice(0, 3).join('')}
        <div class="rail-label">Platform</div>
        ${railItems.slice(3).join('')}
        <div style="margin-top:auto"></div>
      </nav>
      <main class="main" id="main" tabindex="-1"><div class="main-inner">${content}</div></main>
      <footer class="statusbar">
        <span class="dot" aria-hidden="true"></span><span>Engine ABI 2 · runtime 0.2.0</span>
        <span class="sep"></span><span>${D.games.length} games · ${D.games.filter((g) => g.state === 'playable' && !g.synthetic).length} playable</span>
        <span class="sep"></span><span>${D.jobs.filter((j) => j.status === 'running').length} active job</span>
        <span class="sep"></span><span>${state.devMode ? 'Developer mode ON' : 'Developer mode off'}</span>
        <span style="margin-left:auto" class="muted">Interactive prototype — simulated data</span>
      </footer>
    </div>`;
  }
  function demoBanner() {
    return `<div class="banner banner-demo">${icon('info')}<span><strong>Prototype.</strong> States, jobs, sessions and execution results on this screen are simulated demonstrations of the intended REPS2 experience. They are not produced by the engine.</span></div>`;
  }

  // ---------------------------------------------------------------- screens
  function screenLibrary() {
    const games = visibleGames();
    const filters = [{ id: 'all', label: 'All' }].concat(D.lifecycle.map((s) => ({ id: s.id, label: s.label })), D.failureStates.map((s) => ({ id: s.id, label: s.label })));
    const toolbar = `
      <div class="toolbar">
        <div class="seg" role="group" aria-label="View mode">
          <button data-act="view" data-mode="grid" aria-pressed="${state.viewMode === 'grid'}">${icon('grid', 'icon-sm')} Grid</button>
          <button data-act="view" data-mode="list" aria-pressed="${state.viewMode === 'list'}">${icon('list', 'icon-sm')} List</button>
        </div>
        <label class="sr-only" for="sort">Sort</label>
        <select class="select" id="sort" data-act="sort" style="width:180px">
          <option value="recent" ${state.sort === 'recent' ? 'selected' : ''}>Recently processed</option>
          <option value="title" ${state.sort === 'title' ? 'selected' : ''}>Title (A–Z)</option>
          <option value="serial" ${state.sort === 'serial' ? 'selected' : ''}>Serial</option>
          <option value="status" ${state.sort === 'status' ? 'selected' : ''}>Compatibility status</option>
        </select>
        <div style="flex:1"></div>
        <span class="muted">${games.length} of ${D.games.length}</span>
      </div>
      <div class="toolbar" role="group" aria-label="Filter by compatibility state">
        ${filters.map((f) => `<button class="chip-filter" data-act="filter" data-filter="${f.id}" aria-pressed="${state.filter === f.id}">${f.label}</button>`).join('')}
      </div>`;

    if (!games.length) return toolbar + emptyLibrary(true);

    const body = state.viewMode === 'grid'
      ? `<div class="grid" role="listbox" aria-label="Game library">${games.map((g) => `
          <button class="card" role="option" data-act="open" data-id="${g.id}" aria-selected="${state.selected === g.id}">
            ${cover(g)}
            <div class="card-body">
              <span class="card-title">${escapeHtml(g.title)}</span>
              <span class="card-sub mono">${escapeHtml(g.serial)} · ${escapeHtml(g.region)}</span>
              <div class="card-row"><span class="pill pill-${statusMeta(g.state).token}">${icon(statusMeta(g.state).icon, 'icon')}<span>${statusMeta(g.state).label}</span></span>
                <span class="muted mono" style="font-size:11px">${g.analysis ? (g.analysis.instructions / 1000).toFixed(0) + 'k insns' : '—'}</span></div>
            </div>
          </button>`).join('')}</div>`
      : `<div role="listbox" aria-label="Game library" style="display:flex;flex-direction:column;gap:8px">${games.map((g) => `
          <button class="row" role="option" data-act="open" data-id="${g.id}" aria-selected="${state.selected === g.id}">
            ${cover(g, 'row-thumb')}
            <span><span class="card-title">${escapeHtml(g.title)}</span><br><span class="card-sub mono">${escapeHtml(g.serial)}</span></span>
            <span class="muted">${escapeHtml(g.region)}</span>
            <span class="pill pill-${statusMeta(g.state).token}">${icon(statusMeta(g.state).icon, 'icon')}<span>${statusMeta(g.state).label}</span></span>
            <span class="muted mono" style="font-size:11px">${g.analysis ? g.analysis.blocks.toLocaleString() + ' blk' : '—'}</span>
          </button>`).join('')}</div>`;

    const sel = gameById(state.selected);
    return `
      <div class="page-head"><div><h1>Library</h1><p>Your owned PS2 titles and their truthful compatibility state.</p></div>
        <button class="btn btn-primary" data-act="nav" data-route="addgame">${icon('plus', 'icon-sm')} Add Game</button></div>
      ${demoBanner()}
      <div class="detail">
        <div>${toolbar}${body}</div>
        <aside>${sel ? inspector(sel) : ''}</aside>
      </div>`;
  }

  function emptyLibrary(filtered) {
    return `<div class="empty">
      <div class="empty-art">${icon('library', 'icon-lg')}</div>
      <h2>${filtered ? 'No games match this filter' : 'Your library is empty'}</h2>
      <p>${filtered ? 'Adjust the search or filter to see more titles.' : 'Add a legally owned PS2 ISO. REPS2 identifies the disc, extracts the boot executable internally, analyzes and recompiles it, and registers the title here — no manual file extraction required.'}</p>
      ${filtered ? `<button class="btn" data-act="filter" data-filter="all">Clear filter</button>` : `<button class="btn btn-primary" data-act="nav" data-route="addgame">${icon('plus', 'icon-sm')} Add Game</button>`}
    </div>`;
  }

  function inspector(g) {
    return `<div class="inspector">
      <h3>${escapeHtml(g.title)}</h3>
      <div style="margin-bottom:12px">${pill(g.state)}</div>
      <dl class="kv">
        <dt>Serial</dt><dd class="mono">${escapeHtml(g.serial)}</dd>
        <dt>Region</dt><dd>${escapeHtml(g.region)}</dd>
        <dt>ISO size</dt><dd class="mono">${g.iso.sizeGiB} GiB</dd>
        <dt>Boot entry</dt><dd class="mono">${escapeHtml(g.elf.entry)}</dd>
        <dt>Instructions</dt><dd class="mono">${g.analysis ? g.analysis.instructions.toLocaleString() : '—'}</dd>
        <dt>Unsupported</dt><dd class="mono">${g.analysis ? g.analysis.unsupported.toLocaleString() : '—'}</dd>
      </dl>
      <div style="display:flex;gap:8px;margin-top:16px">
        <button class="btn btn-primary btn-sm" data-act="open-detail" data-id="${g.id}">Open details</button>
        ${nextActions(g).slice(0, 1).map((a) => `<button class="btn btn-sm ${a.kind}" data-act="${a.action}" data-id="${g.id}">${a.label}</button>`).join('')}
      </div>
    </div>`;
  }

  function screenDetail() {
    const g = gameById(state.params.id || state.selected);
    if (!g) return emptyLibrary(false);
    const tab = state.params.tab || 'overview';
    const actions = nextActions(g).map((a) => `<button class="btn ${a.kind}" data-act="${a.action}" data-id="${g.id}">${icon(a.icon, 'icon-sm')} ${a.label}</button>`).join('');
    const tabs = [['overview', 'Overview'], ['compatibility', 'Compatibility'], ['evidence', 'Evidence'], ['limitations', 'Limitations']];
    let panel;
    if (tab === 'overview') panel = detailOverview(g);
    else if (tab === 'compatibility') panel = detailCompatibility(g);
    else if (tab === 'evidence') panel = detailEvidence(g);
    else panel = detailLimitations(g);
    return `
      <div style="display:flex;align-items:center;gap:8px;margin-bottom:12px">
        <button class="btn btn-sm btn-ghost" data-act="nav" data-route="library">${icon('back', 'icon-sm')} Library</button>
        <span class="muted">/</span><span>${escapeHtml(g.title)}</span>
      </div>
      ${g.synthetic ? `<div class="banner banner-warn">${icon('info')}<span>Sample title — <strong>synthetic demonstration record</strong>, not real compatibility evidence.</span></div>` : ''}
      <div class="detail">
        <div>
          <div class="detail-hero">
            ${cover(g)}
            <div>
              <h1 style="margin:0 0 4px">${escapeHtml(g.title)}</h1>
              <div class="muted mono" style="margin-bottom:12px">${escapeHtml(g.serial)} · ${escapeHtml(g.region)}</div>
              <div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:16px">${pill(g.state)}</div>
              <div style="display:flex;gap:8px;flex-wrap:wrap">${actions}</div>
              ${playNote(g)}
            </div>
          </div>
          <div class="tabs" role="tablist">
            ${tabs.map(([id, label]) => `<button role="tab" aria-selected="${tab === id}" data-act="tab" data-tab="${id}" data-id="${g.id}">${label}</button>`).join('')}
          </div>
          ${panel}
        </div>
        <aside>${inspector(g)}</aside>
      </div>`;
  }

  function detailOverview(g) {
    return `<div class="split">
      <div class="subtle-card"><h3 style="margin-top:0">Identity</h3>
        <dl class="kv">
          <dt>Disc ISO SHA-256</dt><dd class="mono" style="word-break:break-all">${escapeHtml(g.iso.sha)}</dd>
          <dt>Boot ELF SHA-256</dt><dd class="mono" style="word-break:break-all">${escapeHtml(g.elf.sha)}</dd>
          <dt>Entry point</dt><dd class="mono">${escapeHtml(g.elf.entry)}</dd>
          <dt>Source path</dt><dd class="mono" style="word-break:break-all">${escapeHtml(g.path)}</dd>
        </dl>
      </div>
      <div class="subtle-card"><h3 style="margin-top:0">Build identity</h3>
        ${g.identity ? `<dl class="kv">
          <dt>Runtime ABI</dt><dd class="mono">${g.identity.abi}</dd>
          <dt>Engine</dt><dd class="mono">${g.identity.engine}</dd>
          <dt>Configuration</dt><dd class="mono">${g.identity.config}</dd>
          <dt>Compiler</dt><dd class="mono">${escapeHtml(g.identity.compiler)}</dd>
          <dt>Cache key</dt><dd class="mono">${escapeHtml(g.identity.cacheKey)}</dd>
          <dt>RAM image</dt><dd class="mono">${g.identity.ramMiB} MiB</dd>
        </dl>` : `<p class="muted">No native build identity yet. A build identity is written only after the native project is compiled and linked.</p>`}
      </div>
    </div>
    ${g.analysis ? `<h3 class="section-title">Analysis coverage</h3>
    <div class="stats">
      <div class="stat"><div class="stat-label">Basic blocks</div><div class="stat-value">${g.analysis.blocks.toLocaleString()}</div></div>
      <div class="stat"><div class="stat-label">Guest instructions</div><div class="stat-value">${g.analysis.instructions.toLocaleString()}</div></div>
      <div class="stat"><div class="stat-label">Roots</div><div class="stat-value">${g.analysis.roots.toLocaleString()}</div></div>
      <div class="stat"><div class="stat-label">Unresolved edges</div><div class="stat-value ${g.analysis.unresolved > 500 ? 'warn' : ''}">${g.analysis.unresolved.toLocaleString()}</div>${g.analysis.unresolved > 500 ? `<span class="stat-flag">${icon('alert', 'icon-sm')} above target</span>` : ''}</div>
      <div class="stat"><div class="stat-label">Unsupported stops</div><div class="stat-value ${g.analysis.unsupported > 1000 ? 'warn' : ''}">${g.analysis.unsupported.toLocaleString()}</div>${g.analysis.unsupported > 1000 ? `<span class="stat-flag">${icon('alert', 'icon-sm')} above target</span>` : ''}</div>
    </div>` : ''}`;
  }

  function detailCompatibility(g) {
    const idx = D.lifecycle.findIndex((s) => s.id === g.state);
    const isFailure = !!FAILURES[g.state];
    const reached = idx >= 0 ? idx : Math.max(0, D.lifecycle.findIndex((s) => s.id === 'native-built'));
    return `<div class="subtle-card">
      <h3 style="margin-top:0">Lifecycle</h3>
      <div class="stepper">
        ${D.lifecycle.map((s, i) => {
          const st = i < reached ? 'succeeded' : i === reached ? (isFailure ? 'failed' : 'succeeded') : 'skipped';
          return `<div class="step" data-state="${st}"${i === reached ? ' aria-current="step"' : ''}>
            <div class="step-node">${icon(st === 'succeeded' ? 'check' : st === 'failed' ? 'alert' : 'chevron', 'icon')}</div>
            <div><div class="step-title">${s.label}</div><div class="step-desc">${s.req}</div></div>
            <div class="muted mono" style="font-size:11px">${st}</div>
          </div>`;
        }).join('')}
      </div>
      <p class="muted" style="margin-bottom:0">A prior stage never implies a later stage. Compiling a module does not make a title Playable; reaching a boot logo does not either.</p>
    </div>${isFailure ? `<div class="banner banner-warn" style="margin-top:16px">${icon('alert')}<span><strong>${statusMeta(g.state).label}.</strong> ${g.error ? escapeHtml(g.error.detail) : (g.recovery ? escapeHtml(g.recovery.reason) : 'A blocking condition prevents forward progress.')}</span></div>` : ''}`;
  }

  function detailEvidence(g) {
    return `<div class="subtle-card">
      <h3 style="margin-top:0">Evidence ledger</h3>
      <p class="muted">${escapeHtml(g.evidence)}</p>
      ${g.run && g.run.status !== 'not_run' ? `<h3 class="section-title">Last execution result</h3>
        <dl class="kv">
          <dt>Status</dt><dd class="mono">${escapeHtml(g.run.status)}</dd>
          <dt>Guest instructions</dt><dd class="mono">${(g.run.executed || 0).toLocaleString()}</dd>
          <dt>Stop PC</dt><dd class="mono">${escapeHtml(g.run.pc || '—')}</dd>
          ${g.run.syscall ? `<dt>Boundary</dt><dd class="mono">${escapeHtml(g.run.syscall)}</dd>` : ''}
          ${g.run.note ? `<dt>Note</dt><dd>${escapeHtml(g.run.note)}</dd>` : ''}
        </dl>
        <div style="margin-top:12px"><button class="btn btn-sm" data-act="open-report" data-id="${g.id}">Open execution report</button></div>`
        : `<p class="muted">No execution evidence recorded for the current runtime.</p>`}
    </div>`;
  }

  function detailLimitations(g) {
    return `<div class="subtle-card">
      <h3 style="margin-top:0">Known limitations &amp; blockers</h3>
      ${g.limitations && g.limitations.length ? `<ul>${g.limitations.map((l) => `<li>${escapeHtml(l)}</li>`).join('')}</ul>` : '<p class="muted">None recorded.</p>'}
      <p class="muted" style="margin-bottom:0">Limitations are disclosed, never hidden. Unsupported runtime services produce structured diagnostics rather than silent success.</p>
    </div>`;
  }

  function screenAddGame() {
    return `
      <div class="page-head"><div><h1>Add Game</h1><p>The only required input is one legally owned PS2 ISO.</p></div></div>
      <div class="split">
        <div class="subtle-card">
          <h3 style="margin-top:0">One-ISO workflow</h3>
          <ol class="muted">
            <li>You select an owned ISO.</li>
            <li>REPS2 identifies the disc and boot executable automatically.</li>
            <li>REPS2 analyzes and statically recompiles supported guest code.</li>
            <li>REPS2 builds a native module and links the shared runtime.</li>
            <li>The title appears in the library with a truthful status.</li>
          </ol>
          <button class="btn btn-primary" data-act="open-add" style="margin-top:8px">${icon('plus', 'icon-sm')} Select ISO…</button>
          <p class="muted" style="margin-bottom:0">No BOOT.ELF extraction, no build folders, no developer commands, and no proprietary BIOS required.</p>
        </div>
        <div class="subtle-card">
          <h3 style="margin-top:0">Supported input</h3>
          <dl class="kv">
            <dt>Format</dt><dd>Raw ISO 9660, 2048-byte sectors</dd>
            <dt>Boot resolution</dt><dd>SYSTEM.CNF BOOT2 → BOOT</dd>
            <dt>Executable</dt><dd>ELF32 LE EM_MIPS ET_EXEC</dd>
            <dt>Not supported</dt><dd>BIN/CUE (2352), CHD, UDF, Joliet/Rock Ridge, multi-volume</dd>
          </dl>
          <p class="muted" style="margin-bottom:0">Duplicate imports are detected by content hash; the original ISO is referenced by path, never copied.</p>
        </div>
      </div>`;
  }

  function screenPipeline() {
    const g = gameById(state.params.id || 'g-downhill') || D.games[0];
    const p = state.pipeline && state.pipeline.gameId === g.id ? state.pipeline : { gameId: g.id, index: D.pipelineStages.length, running: false };
    const rows = D.pipelineStages.map((s, i) => {
      let st = 'pending';
      if (i < p.index) st = 'succeeded';
      else if (i === p.index && p.running) st = 'running';
      else if (i === p.index && p.index >= D.pipelineStages.length) st = 'succeeded';
      return `<div class="step" data-state="${st}"${st === 'running' ? ' aria-current="step"' : ''}>
        <div class="step-node">${icon(st === 'succeeded' ? 'check' : st === 'running' ? 'refresh' : 'chevron', 'icon')}</div>
        <div><div class="step-title">${s.label}</div><div class="step-desc">${s.desc}</div>
        ${st === 'running' ? '<div class="progress indeterminate"><i></i></div>' : ''}</div>
        <div class="muted mono" style="font-size:11px">${st}</div>
      </div>`;
    }).join('');
    return `
      <div class="page-head"><div><h1>Import &amp; Build</h1><p>Transparent pipeline stages for <strong>${escapeHtml(g.title)}</strong>.</p></div>
        <div style="display:flex;gap:8px">
          <button class="btn" data-act="pipe-reset" data-id="${g.id}">Reset</button>
          <button class="btn btn-primary" data-act="pipe-run" data-id="${g.id}" ${p.running ? 'disabled' : ''}>${icon('play2', 'icon-sm')} Simulate pipeline</button>
        </div></div>
      <div class="banner banner-demo">${icon('info')}<span><strong>Prototype simulation.</strong> Advancing stages here is a UI demonstration. Real stages are emitted by the frontend commands (<span class="mono">elf.parse</span>, <span class="mono">cfg.discover</span>, <span class="mono">ir.lift</span>, <span class="mono">codegen.emit_cpp</span>, …) with no fabricated progress percentages.</span></div>
      <div class="split">
        <div class="subtle-card"><div class="stepper">${rows}</div></div>
        <div>
          <div class="console" id="pipe-log"><span class="ln-ts">[demo]</span> <span class="ln-info">stage attribution is coarse-grained; per-instruction progress is not reported. Host CMake compile and module validation follow the frontend stage set.</span>${
            p.index > 0 ? D.pipelineStages.slice(0, p.index).map((s) => `\n<span class="ln-ts">[demo]</span> <span class="ln-ok">pipeline.stage.succeeded stage=${s.id}</span>`).join('') : ''
          }</div>
          <div class="subtle-card" style="margin-top:16px"><h3 style="margin-top:0">Artifacts on success</h3>
            <dl class="kv">
              <dt>game.json</dt><dd class="mono">library manifest, schema v1</dd>
              <dt>*.manifest.json</dt><dd class="mono">build manifest, schema v3</dd>
              <dt>*.ram.bin</dt><dd class="mono">32 MiB initial EE RAM image</dd>
              <dt>*.native/</dt><dd class="mono">CMake project + probe</dd>
              <dt>build identity</dt><dd class="mono">cache_key_sha256</dd>
            </dl>
          </div>
        </div>
      </div>`;
  }

  function screenQueue() {
    return `
      <div class="page-head"><div><h1>Build Queue</h1><p>In-flight and recent analysis / translation / build jobs across titles.</p></div>
        <button class="btn" data-act="nav" data-route="pipeline" data-id="g-downhill">${icon('layers', 'icon-sm')} Open pipeline</button></div>
      ${demoBanner()}
      <div class="subtle-card">
        <table class="table">
          <thead><tr><th scope="col">Job</th><th scope="col">Title</th><th scope="col">Stage</th><th scope="col">Status</th><th scope="col">Started</th><th scope="col">Duration</th><th scope="col">Note</th></tr></thead>
          <tbody>${D.jobs.map((j) => `<tr>
            <td class="num">${escapeHtml(j.id)}</td>
            <td>${escapeHtml(j.game)}</td>
            <td class="num">${escapeHtml(j.stage)}</td>
            <td>${j.status === 'running' ? `<span class="pill pill-info">${icon('refresh', 'icon')}<span>running</span></span>` : j.status === 'failed' ? `<span class="pill pill-danger">${icon('alert', 'icon')}<span>failed</span></span>` : `<span class="pill pill-playable">${icon('check', 'icon')}<span>succeeded</span></span>`}</td>
            <td class="num">${escapeHtml(j.started)}</td>
            <td class="num">${escapeHtml(j.dur)}</td>
            <td class="muted">${escapeHtml(j.note)}</td>
          </tr>`).join('')}</tbody>
        </table>
      </div>
      <p class="muted" style="margin-top:12px">A persistent build queue is <strong>proposed</strong> (M7). Today jobs are per-invocation CLI commands; this screen shows the intended aggregation.</p>`;
  }

  function screenDeveloper() {
    return `
      <div class="page-head"><div><h1>Developer Center</h1><p>Engine, analysis and session diagnostics backed by versioned JSON reports.</p></div>
        <button class="btn" data-act="dev-caps">${icon('terminal', 'icon-sm')} dev capabilities</button></div>
      ${demoBanner()}
      ${state.params.caps ? `<div class="subtle-card" style="margin-bottom:16px"><h3 style="margin-top:0">dev capabilities (schema_version 1)</h3>
        <div class="console">${escapeHtml(JSON.stringify({
          kind: 'reps2.developer.capabilities', tool_version: '0.7.0', schema_version: 1,
          report_schema: 'developer-report-v1',
          kinds: ['reps2.developer.session_list', 'reps2.developer.session_inspection', 'reps2.developer.session_summary', 'reps2.developer.session_comparison', 'reps2.developer.capabilities', 'reps2.developer.error'],
          diagnostic_codes: ['REPS2_PIPELINE_STAGE_FAILED', 'REPS2_COMMAND_FAILED', 'REPS2_DEV_INVALID_ARGUMENTS', 'REPS2_DEV_SESSION_LIST_FAILED', 'REPS2_DEV_SESSION_INSPECTION_FAILED', 'REPS2_DEV_SUMMARY_UNAVAILABLE']
        }, null, 2))}</div></div>` : ''}
      <h3 class="section-title">Sessions</h3>
      <div class="subtle-card">
        <table class="table">
          <thead><tr><th scope="col">Session</th><th scope="col">Command</th><th scope="col">Outcome</th><th scope="col">Events</th><th scope="col">Diagnostics</th><th scope="col">Started</th><th scope="col"></th></tr></thead>
          <tbody>${D.sessions.map((s) => `<tr>
            <td class="num">${escapeHtml(s.id)}</td>
            <td class="num">${escapeHtml(s.command)}</td>
            <td>${s.outcome === 'succeeded' ? `<span class="pill pill-playable">${icon('check', 'icon')}<span>succeeded</span></span>` : `<span class="pill pill-danger">${icon('alert', 'icon')}<span>failed</span></span>`}</td>
            <td class="num">${s.events}</td>
            <td class="num">${s.diagnostics}</td>
            <td class="num">${escapeHtml(s.started)}</td>
            <td><button class="btn btn-sm btn-ghost" data-act="open-report" data-id="g-downhill">Inspect</button></td>
          </tr>`).join('')}</tbody>
        </table>
      </div>
      <div class="split" style="margin-top:16px">
        <div class="subtle-card"><h3 style="margin-top:0">What is instrumented</h3>
          <ul class="muted"><li>Frontend command lifecycle + coarse pipeline stages</li><li>ELF parse / target validation / RAM image / CFG / IR / codegen / native project</li><li>Engine run report: status, PC, COP0 registers, HLE syscalls, instruction trace</li></ul></div>
        <div class="subtle-card"><h3 style="margin-top:0">What is not claimed</h3>
          <ul class="muted"><li>No per-guest-instruction disk I/O</li><li>No invented device/GS/SPU telemetry</li><li>Unsupported services are reported, never faked</li></ul></div>
      </div>`;
  }

  function screenReport() {
    const g = gameById(state.params.id || 'g-downhill') || D.games[0];
    const r = g.run || { status: 'not_run' };
    const bars = [30, 55, 40, 72, 60, 88, 66, 95, 80, 70, 92, 58];
    return `
      <div style="display:flex;align-items:center;gap:8px;margin-bottom:12px">
        <button class="btn btn-sm btn-ghost" data-act="nav" data-route="developer">${icon('back', 'icon-sm')} Developer Center</button>
        <span class="muted">/</span><span>Execution report — ${escapeHtml(g.title)}</span>
      </div>
      ${demoBanner()}
      <div class="subtle-card">
        <div style="display:flex;justify-content:space-between;align-items:center">
          <div><h3 style="margin:0">rg.reps2.engine.run_report.v1</h3><span class="muted mono">${escapeHtml(g.serial)}</span></div>
          ${r.status && r.status !== 'not_run' && r.status !== 'not_tested' ? `<span class="pill pill-${r.status === 'session_ok' ? 'playable' : 'blocked'}">${icon(r.status === 'session_ok' ? 'check' : 'lock', 'icon')}<span>${escapeHtml(r.status)}</span></span>` : `<span class="pill pill-neutral">${icon('info', 'icon')}<span>no run</span></span>`}
        </div>
      </div>
      <h3 class="section-title">Execution summary</h3>
      <div class="stats">
        <div class="stat"><div class="stat-label">Guest instructions</div><div class="stat-value">${(r.executed || 0).toLocaleString()}</div></div>
        <div class="stat"><div class="stat-label">Budget</div><div class="stat-value">${(r.budget || 0).toLocaleString()}</div></div>
        <div class="stat"><div class="stat-label">Stop PC</div><div class="stat-value mono" style="font-size:16px">${escapeHtml(r.pc || '—')}</div></div>
        <div class="stat"><div class="stat-label">Boundary</div><div class="stat-value mono" style="font-size:16px">${escapeHtml(r.syscall || '—')}</div></div>
      </div>
      <div class="split" style="margin-top:16px">
        <div class="subtle-card"><h3 style="margin-top:0">COP0 state</h3>
          <dl class="kv">
            <dt>Cause</dt><dd class="mono">${escapeHtml(r.cause || '—')}</dd>
            <dt>EPC</dt><dd class="mono">${escapeHtml(r.epc || '—')}</dd>
            <dt>Status</dt><dd class="mono">${escapeHtml(r.cop0Status || '—')}</dd>
            <dt>BadVAddr</dt><dd class="mono">${escapeHtml(r.badvaddr || '—')}</dd>
          </dl>
          <p class="muted" style="margin-bottom:0;font-size:12px">COP0 registers come from the generated native probe trace line and are shown when the probe reports them; the engine run report ("rg.reps2.engine.run_report.v1") separately carries phase, PC and instruction counters.</p>
        </div>
        <div class="subtle-card"><h3 style="margin-top:0">Instructions executed (bounded sample)</h3>
          <div class="timeline" role="img" aria-label="Illustrative per-batch instruction distribution">${bars.map((b, i) => `<div class="bar" style="height:${b}%" title="Batch ${i + 1}: ${(b * 12345).toLocaleString()}"></div>`).join('')}</div>
          <p class="muted" style="margin-bottom:0;font-size:12px">Illustrative shape only. The report exposes counters, not a full trace.</p>
        </div>
      </div>
      <h3 class="section-title">Syscall / boundary observations</h3>
      <div class="console">${(r.syscall ? `<span class="ln-warn">[boundary]</span> <span class="ln-info">number=${escapeHtml(r.syscall.split(' ')[0])} name=${escapeHtml(r.syscall.split(' ')[1] || '')} pc=${escapeHtml(r.pc || '')}</span>
` : '')}<span class="ln-info">[boundary] HLE services implemented today (runtime/include/reps2/runtime/ee_hle.hpp): AddDmacHandler(0x12), RemoveDmacHandler(0x13), EnableDmac(0x16), DisableDmac(0x17), CreateThread(0x20), StartThread(0x22), ChangeThreadPriority(0x29), GetThreadId(0x2F), ReferThreadStatus(0x30), SetupThread(0x3C), SetupHeap(0x3D), CreateSema(0x40), DeleteSema(0x41), SignalSema(0x42), iSignalSema(0x43), WaitSema(0x44), PollSema(0x45), iPollSema(0x46), SetOsdConfigParam(0x4A), GetOsdConfigParam(0x4B), GetEntryAddress(0x5B), FlushCache(0x64), SifStopDma(0x6B), SetSyscall(0x74), SifSetDma(0x77), SifSetDChain(0x78), SifSetReg(0x79), SifGetReg(0x7A), FindAddress(0x83)</span>
<span class="ln-error">[blocked]</span> <span class="ln-info">status=${escapeHtml(r.status || 'not_run')} ${r.note ? '— ' + escapeHtml(r.note) : ''}</span></div>
      <div style="margin-top:16px;display:flex;gap:8px"><button class="btn" data-act="demo-copy">${icon('copy', 'icon-sm')} Export report JSON</button><button class="btn btn-ghost" data-act="nav" data-route="library">Back to library</button></div>`;
  }

  function screenSettings() {
    const sw = (id, label, desc, on) => `<div class="setting-row">
      <div class="setting-copy"><strong>${label}</strong><span>${desc}</span></div>
      <label class="switch"><input type="checkbox" data-act="toggle" data-key="${id}" ${on ? 'checked' : ''} aria-label="${escapeAttr(label)}"><span class="track"></span><span class="thumb"></span></label>
    </div>`;
    return `
      <div class="page-head"><div><h1>Settings</h1><p>Implemented controls are enabled; future capabilities are disabled and labelled.</p></div></div>
      <div class="split">
        <div>
          <div class="setting-group"><h3 class="section-title" style="margin-top:0">Paths</h3>
            <div class="field" style="margin-bottom:12px"><label for="s-lib">Game library root</label><input class="input mono" id="s-lib" data-act="setting" data-key="libraryRoot" value="${escapeAttr(state.settings.libraryRoot)}"></div>
            <div class="field" style="margin-bottom:12px"><label for="s-build">Native build storage</label><input class="input mono" id="s-build" data-act="setting" data-key="buildRoot" value="${escapeAttr(state.settings.buildRoot)}"></div>
            <div class="field"><label for="s-log">Diagnostics log root</label><input class="input mono" id="s-log" data-act="setting" data-key="logRoot" value="${escapeAttr(state.settings.logRoot)}"></div>
          </div>
          <h3 class="section-title">Runtime</h3>
          ${sw('hleSyscalls', 'EE syscall HLE layer', 'Enable the host service layer for the generated module.', state.settings.hleSyscalls)}
          ${sw('instructionTrace', 'Instruction trace (bounded)', 'Emit up to 16 guest PCs per bounded run.', state.settings.instructionTrace)}
          <div class="setting-row"><div class="setting-copy"><strong>Default execution budget</strong><span>Guest instructions per bounded session.</span></div>
            <input class="input mono" style="width:160px" data-act="setting" data-key="defaultBudget" value="${state.settings.defaultBudget}"></div>
          <h3 class="section-title">Input &amp; accessibility</h3>
          ${sw('controller', 'Controller navigation', 'Map gamepad input onto the same focus model as the keyboard.', state.settings.controller)}
          ${sw('reducedMotion', 'Reduce motion', 'Collapse transitions and disable non-essential animation.', state.settings.reducedMotion)}
        </div>
        <div>
          <div class="setting-group"><h3 class="section-title" style="margin-top:0">Diagnostics &amp; developer</h3>
            ${sw('devMode', 'Developer mode', 'Enable the Developer Center and session diagnostics (currently a prototype toggle).', state.devMode)}
          </div>
          <div class="setting-group"><h3 class="section-title">Display &amp; audio (future)</h3>
            <div class="setting-row"><div class="setting-copy"><strong>Resolution scaling</strong><span>Requires the GS/GIF renderer.</span></div><button class="btn btn-sm" disabled>Not implemented</button></div>
            <div class="setting-row"><div class="setting-copy"><strong>Audio output</strong><span>Requires SPU2.</span></div><button class="btn btn-sm" disabled>Not implemented</button></div>
            <div class="setting-row"><div class="setting-copy"><strong>BIOS image</strong><span>Not required. REPS2 uses clean-room HLE; it never distributes firmware.</span></div><span class="pill pill-playable">${icon('check', 'icon')}<span>not required</span></span></div>
          </div>
          <div class="subtle-card"><h3 style="margin-top:0">Storage layout</h3><pre class="console" style="margin:0">%LOCALAPPDATA%\\REPS2\\Library\\games\\sha256-&lt;digest&gt;\\{game.json, boot.elf, recompiled.cpp, ee-main-ram.bin}
%LOCALAPPDATA%\\REPS2\\Builds\\&lt;cache_key&gt;\\
%LOCALAPPDATA%\\REPS2\\Logs\\session-*\\{session.log, events.jsonl, session.json, summary.json}</pre></div>
        </div>
      </div>`;
  }

  // ---------------------------------------------------------------- render
  function render() {
    const content = {
      library: screenLibrary, detail: screenDetail, addgame: screenAddGame,
      pipeline: screenPipeline, queue: screenQueue, developer: screenDeveloper,
      report: screenReport, settings: screenSettings
    }[state.route]();
    $('#app').innerHTML = renderShell(content);
    if (state.route === 'library') { const s = $('#global-search'); if (s) s.value = state.query; }
    renderDialog();
  }

  // ---------------------------------------------------------------- actions
  function navigate(route, params = {}) {
    state.route = route; state.params = params;
    if (route === 'pipeline' && params.id) state.pipeline = { gameId: params.id, index: 0, running: false };
    const qs = new URLSearchParams(params).toString();
    const h = '#' + route + (qs ? '?' + qs : '');
    if (location.hash !== h) history.replaceState(null, '', h);
    render();
    const m = $('#main'); if (m) m.scrollTop = 0;
  }

  document.addEventListener('click', (e) => {
    const t = e.target.closest('[data-act]');
    if (!t) return;
    const act = t.dataset.act;
    const id = t.dataset.id;
    switch (act) {
      case 'nav': e.preventDefault(); navigate(t.dataset.route, { id }); break;
      case 'view': state.viewMode = t.dataset.mode; render(); break;
      case 'filter': state.filter = t.dataset.filter; render(); break;
      case 'open': state.selected = id; render(); break;
      case 'open-detail': navigate('detail', { id, tab: 'overview' }); break;
      case 'tab': navigate('detail', { id, tab: t.dataset.tab }); break;
      case 'open-report': navigate('report', { id }); break;
      case 'toggle-rail': state.railCollapsed = !state.railCollapsed; render(); break;
      case 'open-add': openDialog({ kind: 'addGame', title: 'Add Game — select ISO' }); break;
      case 'play': toast('Prototype: launching a session requires the Playable state and a real runtime.', 'info'); break;
      case 'run': navigate('report', { id }); toast('Prototype: simulates a bounded execution session.', 'info'); break;
      case 'analyze': case 'translate': case 'build': navigate('pipeline', { id }); break;
      case 'relink': openDialog({ kind: 'relink', title: 'Relink source ISO', expectedSha: (gameById(id) || {}).recovery ? gameById(id).recovery.expectedSha : '' }); break;
      case 'export': openDialog({ kind: 'errorDetail', title: 'Structured failure event', code: ((gameById(id) || {}).error || {}).code || 'REPS2_COMMAND_FAILED', stage: ((gameById(id) || {}).error || {}).stage || 'native.compile', detail: ((gameById(id) || {}).error || {}).detail || 'No structured failure recorded for this record.' }); break;
      case 'reveal': toast('Prototype: would open the source ISO location in Explorer.', 'info'); break;
      case 'pipe-run': simulatePipeline(id); break;
      case 'pipe-reset': state.pipeline = { gameId: id, index: 0, running: false }; render(); break;
      case 'dev-caps': navigate('developer', { caps: true }); break;
      case 'scrim': if (e.target === t) closeDialog(); break;
      case 'close-dialog': closeDialog(); break;
      case 'demo-import': case 'demo-relink': case 'demo-copy':
        closeDialog(); toast('Prototype demonstration — no real file was read or written.', 'info'); break;
    }
  });
  document.addEventListener('change', (e) => {
    const t = e.target.closest('[data-act]');
    if (!t) return;
    if (t.dataset.act === 'sort') { state.sort = t.value; render(); }
    if (t.dataset.act === 'toggle') {
      const key = t.dataset.key;
      if (key === 'devMode') state.devMode = t.checked;
      else state.settings[key] = t.checked;
      if (key === 'reducedMotion') document.documentElement.style.setProperty('--dur-micro', t.checked ? '1ms' : '150ms');
      render();
    }
    if (t.dataset.act === 'setting') { state.settings[t.dataset.key] = t.value; }
  });
  document.addEventListener('input', (e) => {
    if (e.target.id === 'global-search') {
      state.query = e.target.value;
      const games = visibleGames();
      const inner = $('.main-inner');
      if (inner && state.route === 'library') {
        // re-render only the grid area to keep focus
        inner.innerHTML = screenLibrary();
        const s = $('#global-search'); if (s) { s.focus(); s.setSelectionRange(s.value.length, s.value.length); }
      }
    }
  });

  function simulatePipeline(id) {
    const stages = D.pipelineStages.length;
    state.pipeline = { gameId: id, index: 0, running: true };
    render();
    let i = 0;
    const tick = () => {
      i += 1;
      state.pipeline = { gameId: id, index: i, running: i < stages };
      render();
      if (i < stages) setTimeout(tick, 380);
      else toast('Prototype: simulated pipeline complete — no artifacts were produced.', 'info');
    };
    setTimeout(tick, 380);
  }

  // ---------------------------------------------------------------- key nav
  document.addEventListener('keydown', (e) => {
    if (state.dialog) return;
    if (e.key === '/' && document.activeElement.tagName !== 'INPUT') { e.preventDefault(); const s = $('#global-search'); if (s) s.focus(); }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
      e.preventDefault();
      navigate('library'); toast('Prototype: command palette (Ctrl K) would open here.', 'info');
    }
  });

  // ---------------------------------------------------------------- boot
  function applyHash() {
    const raw = location.hash.replace(/^#/, '');
    if (!raw) { navigate('library'); return; }
    const [route, qs] = raw.split('?');
    const params = {};
    for (const [k, v] of new URLSearchParams(qs || '').entries()) params[k] = v;
    if (params.id) state.selected = params.id;
    if (params.filter) state.filter = params.filter;
    if (params.view) state.viewMode = params.view;
    state.route = route || 'library';
    state.params = params;
    if (state.route === 'pipeline' && params.id) state.pipeline = { gameId: params.id, index: 0, running: false };
    render();
    if (params.d === 'add') openDialog({ kind: 'addGame', title: 'Add Game — select ISO' });
    if (params.d === 'error') openDialog({ kind: 'errorDetail', title: 'Structured failure event', code: 'REPS2_COMMAND_FAILED', stage: 'native.compile', detail: 'link.exe : fatal error LNK1120: 1 unresolved externals' });
  }
  window.addEventListener('hashchange', () => {
    const route = location.hash.replace(/^#/, '').split('?')[0];
    if (route !== state.route) applyHash();
  });
  applyHash();
  window.REPS2_APP = { navigate, state, applyHash };
})();
