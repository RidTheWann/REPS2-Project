// SPDX-License-Identifier: GPL-3.0-or-later
// Dependency-free interaction smoke test for the REPS2 product prototype.
// Loaded after app.js; every assertion runs synchronously against the real app.
// Run:
//   chrome --headless=new --disable-gpu --dump-dom \
//     --virtual-time-budget=2000 "file:///<repo>/product-prototype/tests/smoke.html"
// and grep stdout for "SMOKE".

(function () {
  const results = [];
  const $$ = (sel) => Array.from(document.querySelectorAll(sel));
  const APP = window.REPS2_APP;
  const DATA = window.REPS2_DATA;

  function test(name, fn) {
    try {
      const failure = fn();
      if (failure) results.push({ ok: false, name, detail: String(failure) });
      else results.push({ ok: true, name });
    } catch (err) {
      results.push({ ok: false, name, detail: err && err.message ? err.message : String(err) });
    }
  }
  function assert(cond, msg) { if (!cond) throw new Error(msg || 'assertion failed'); }

  function resetLibrary() {
    APP.state.filter = 'all';
    APP.state.query = '';
    APP.state.viewMode = 'grid';
    APP.navigate('library');
  }

  // 1. Library renders every game as a listbox option.
  test('library renders listbox options for all games', () => {
    resetLibrary();
    const listbox = document.querySelector('.grid[role="listbox"]');
    assert(listbox, 'grid listbox missing');
    const cards = $$('.grid[role="listbox"] [role="option"]');
    assert(cards.length === DATA.games.length, `expected ${DATA.games.length} cards, got ${cards.length}`);
    return null;
  });

  // 2. Status pill carries the accessible evidence label.
  test('inspector status pill exposes aria-label with evidence', () => {
    const pill = document.querySelector('.inspector [role="status"], .inspector .pill');
    assert(pill, 'inspector pill missing');
    const label = pill.getAttribute('aria-label') || '';
    assert(label.includes('Native-built'), `pill aria-label lacks state: "${label}"`);
    assert(label.length > 'Native-built'.length, 'pill aria-label lacks evidence summary');
    return null;
  });

  // 3. Selecting a card updates the inspector (single-select model).
  test('clicking a card selects it and updates the inspector', () => {
    const card = document.querySelector('.grid [data-act="open"][data-id="g-black"]');
    assert(card, 'g-black card missing');
    card.click();
    assert(APP.state.selected === 'g-black', `selected=${APP.state.selected}`);
    const h3 = document.querySelector('.inspector h3');
    assert(h3 && h3.textContent.includes('Black'), 'inspector did not follow selection');
    const selected = $$('.grid [role="option"][aria-selected="true"]');
    assert(selected.length === 1, `expected 1 selected option, got ${selected.length}`);
    return null;
  });

  // 4. Filter chips narrow the library by lifecycle state.
  test('state filter narrows the library', () => {
    resetLibrary();
    document.querySelector('[data-act="filter"][data-filter="native-built"]').click();
    const cards = $$('.grid [role="option"]');
    assert(cards.length === 1, `expected 1 native-built card, got ${cards.length}`);
    assert(cards[0].dataset.id === 'g-downhill', `wrong card: ${cards[0].dataset.id}`);
    return null;
  });

  // 5. Search narrows results and keeps keyboard focus in the field.
  test('search filters games and retains input focus', () => {
    resetLibrary();
    const input = document.querySelector('#global-search');
    assert(input, 'search input missing');
    input.value = 'black';
    input.dispatchEvent(new Event('input', { bubbles: true }));
    const cards = $$('.grid [role="option"]');
    assert(cards.length === 1, `expected 1 result, got ${cards.length}`);
    assert(cards[0].dataset.id === 'g-black', `wrong result: ${cards[0].dataset.id}`);
    assert(document.activeElement && document.activeElement.id === 'global-search', 'focus left the search field');
    const fresh = document.querySelector('#global-search');
    fresh.value = '';
    fresh.dispatchEvent(new Event('input', { bubbles: true }));
    return null;
  });

  // 6. Grid/list view toggle swaps the composite widget.
  test('view toggle switches grid to list', () => {
    resetLibrary();
    document.querySelector('[data-act="view"][data-mode="list"]').click();
    assert($$('.row[role="option"]').length === DATA.games.length, 'list rows missing');
    assert(!document.querySelector('.grid[role="listbox"]'), 'grid still rendered in list mode');
    document.querySelector('[data-act="view"][data-mode="grid"]').click();
    return null;
  });

  // 7. Play must not appear before Playable; the next valid action does.
  test('Play is withheld for a Native-built title', () => {
    APP.navigate('detail', { id: 'g-downhill', tab: 'overview' });
    assert(!document.querySelector('[data-act="play"]'), 'Play button rendered for native-built title');
    const run = document.querySelector('[data-act="run"]');
    assert(run && run.textContent.includes('Run bounded session'), 'bounded-session action missing');
    const main = document.querySelector('#main').textContent;
    assert(main.includes('Play becomes available only when a title reaches'), 'Play gating note missing');
    return null;
  });

  // 8. Play is offered exactly for the synthetic Playable sample.
  test('Play appears only for the Playable state', () => {
    APP.navigate('detail', { id: 'g-scc', tab: 'overview' });
    assert(document.querySelector('[data-act="play"]'), 'Play missing on playable sample');
    assert(document.querySelector('.banner-warn'), 'synthetic-record warning missing');
    return null;
  });

  // 9. Detail tabs switch panels and expose aria-selected.
  test('detail tabs switch panels', () => {
    APP.navigate('detail', { id: 'g-downhill', tab: 'overview' });
    document.querySelector('[data-act="tab"][data-tab="evidence"]').click();
    const active = document.querySelector('[role="tab"][aria-selected="true"]');
    assert(active && active.dataset.tab === 'evidence', 'evidence tab not selected');
    return null;
  });

  // 10. Hash routing restores route and params.
  test('hash routing restores a route', () => {
    location.hash = '#queue';
    APP.applyHash();
    assert(APP.state.route === 'queue', `route=${APP.state.route}`);
    assert(document.querySelector('h1').textContent.includes('Build Queue'), 'queue screen not rendered');
    return null;
  });

  // 11. Add Game dialog is modal, Esc closes it, focus returns to invoker.
  test('dialog traps intent and restores focus on close', () => {
    APP.navigate('addgame');
    const opener = document.querySelector('[data-act="open-add"]');
    assert(opener, 'Select ISO button missing');
    opener.focus();
    opener.click();
    const dlg = document.querySelector('#dialog-root [role="dialog"][aria-modal="true"]');
    assert(dlg, 'modal dialog not rendered');
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    assert(!document.querySelector('#dialog-root [role="dialog"]'), 'Escape did not close dialog');
    assert(document.activeElement === opener, 'focus was not restored to the invoker');
    return null;
  });

  // 12. Run action routes to the execution report and raises a toast.
  test('run action opens the report and announces progress', () => {
    APP.navigate('detail', { id: 'g-downhill' });
    document.querySelector('[data-act="run"]').click();
    assert(APP.state.route === 'report', `route=${APP.state.route}`);
    assert(location.hash.startsWith('#report'), `hash=${location.hash}`);
    assert($$('#toasts .toast').length >= 1, 'toast not rendered');
    const main = document.querySelector('#main').textContent;
    assert(main.includes('rg.reps2.engine.run_report.v1'), 'run report schema marker missing');
    return null;
  });

  // 13. Empty filter state offers a recovery action.
  test('empty filter state offers clear-filter recovery', () => {
    resetLibrary();
    document.querySelector('[data-act="filter"][data-filter="boot-tested"]').click();
    const empty = document.querySelector('.empty');
    assert(empty && empty.textContent.includes('No games match this filter'), 'empty state missing');
    const clear = document.querySelector('.empty [data-act="filter"][data-filter="all"]');
    assert(clear, 'clear-filter action missing');
    clear.click();
    assert($$('.grid [role="option"]').length === DATA.games.length, 'clear filter did not restore library');
    return null;
  });

  // 14. Status bar exposes the runtime ABI and truthful playable count.
  test('status bar reports ABI and honest playable count', () => {
    const bar = document.querySelector('.statusbar');
    assert(bar && bar.textContent.includes('Engine ABI 2'), 'ABI marker missing');
    const playableReal = DATA.games.filter((g) => g.state === 'playable' && !g.synthetic).length;
    assert(bar.textContent.includes(`${playableReal} playable`), 'playable count not derived from data');
    return null;
  });

  // 15. Pipeline screen renders the documented frontend stage set.
  test('pipeline screen renders the documented stages', () => {
    APP.navigate('pipeline', { id: 'g-downhill' });
    const steps = $$('.stepper .step');
    assert(steps.length === DATA.pipelineStages.length, `expected ${DATA.pipelineStages.length} stages, got ${steps.length}`);
    const text = document.querySelector('.stepper').textContent;
    assert(text.includes('CFG discovery') && text.includes('Native project'), 'stage labels missing');
    return null;
  });

  const failed = results.filter((r) => !r.ok);
  const summary = `SMOKE: ${failed.length ? 'FAIL' : 'PASS'} ${results.length - failed.length}/${results.length}`;
  const lines = results.map((r) => `${r.ok ? 'PASS' : 'FAIL'} ${r.name}${r.detail ? ' — ' + r.detail : ''}`);
  document.getElementById('smoke-results').textContent = summary + '\n' + lines.join('\n');
  document.title = summary;
})();
