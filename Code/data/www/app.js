// =====================================================================
// Retrofit Switch — Control Panel SPA
// =====================================================================
(() => {
  'use strict';

  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

  // -------------------------------------------------------------
  // API helper
  // -------------------------------------------------------------
  async function api(path, opts = {}) {
    const res = await fetch(path, {
      headers: opts.body ? { 'Content-Type': 'application/json' } : {},
      ...opts,
    });
    let data;
    try { data = await res.json(); } catch (e) { data = { success: res.ok }; }
    if (!res.ok || data.success === false) {
      throw new Error(data.message || `Request failed (${res.status})`);
    }
    return data;
  }
  const apiGet = (path) => api(path);
  const apiPost = (path, body) => api(path, { method: 'POST', body: JSON.stringify(body || {}) });
  const apiPut = (path, body) => api(path, { method: 'PUT', body: JSON.stringify(body || {}) });
  const apiDelete = (path) => api(path, { method: 'DELETE' });

  // -------------------------------------------------------------
  // Toast
  // -------------------------------------------------------------
  let toastTimer = null;
  function toast(message, kind = '') {
    const el = $('#toast');
    el.textContent = message;
    el.className = 'toast show' + (kind ? ' ' + kind : '');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { el.classList.remove('show'); }, 2600);
  }
  function fail(err) { toast(err.message || String(err), 'error'); }

  // -------------------------------------------------------------
  // Tab routing
  // -------------------------------------------------------------

  // Fast position polling while Switches tab is open.
  // This makes the X/Y readout update while the motors are moving.
  let switchesPositionPollTimer = null;
  function startSwitchPositionPolling() {
    if (switchesPositionPollTimer) return;
    switchesPositionPollTimer = setInterval(refreshJogReadouts, 350);
  }
  function stopSwitchPositionPolling() {
    if (!switchesPositionPollTimer) return;
    clearInterval(switchesPositionPollTimer);
    switchesPositionPollTimer = null;
  }

  function showTab(name) {
    $$('.tab-btn').forEach(b => b.classList.toggle('active', b.dataset.tab === name));
    $$('.tab-page').forEach(p => p.classList.toggle('active', p.id === `page-${name}`));
    location.hash = name;
    if (name === 'switches') {
      loadSwitchesTab();
      startSwitchPositionPolling();
    } else {
      stopSwitchPositionPolling();
    }
    if (name === 'schedules') loadSchedulesTab();
    if (name === 'settings') loadSettingsTab();
  }
  $('#tabbar').addEventListener('click', (e) => {
    const btn = e.target.closest('.tab-btn');
    if (btn) showTab(btn.dataset.tab);
  });

  // -------------------------------------------------------------
  // Status polling (LEDs + dashboard strip)
  // -------------------------------------------------------------
  function setLed(el, state) {
    el.classList.remove('on', 'warn', 'busy');
    if (state) el.classList.add(state);
  }

  async function pollStatus() {
    try {
      const s = await apiGet('/api/status');
      const ledHome = $('#ledHome');
      if (s.homing_in_progress) setLed(ledHome, 'busy');
      else if (s.homed) setLed(ledHome, 'on');
      else setLed(ledHome, 'warn');

      const ledSta = $('#ledSta');
      setLed(ledSta, s.wifi.sta_connected ? 'on' : 'warn');

      const ledTime = $('#ledTime');
      setLed(ledTime, s.time.synced ? 'on' : 'warn');

      $('#statTime').textContent = s.time.now || '—';
      $('#statPos').textContent = `X ${s.position.x.toFixed(1)}  Y ${s.position.y.toFixed(1)} mm`;
      $('#statNet').textContent = s.wifi.sta_connected
        ? `STA · ${s.wifi.sta_ip}`
        : `AP only · ${s.wifi.ap_ip}`;
    } catch (e) {
      // status polling failures are silent — the device may be mid-reboot
    }
  }
  setInterval(pollStatus, 5000);
  pollStatus();

  // -------------------------------------------------------------
  // Shared switch data cache
  // -------------------------------------------------------------
  let switchesCache = { switches: [], count: 0, type: 0 };
  async function refreshSwitches() {
    switchesCache = await apiGet('/api/switches');
    return switchesCache;
  }

  // ===================== DASHBOARD =====================
  async function loadDashboard() {
    try {
      await refreshSwitches();
      renderDashboard();
    } catch (e) { fail(e); }
  }

  function defaultSwitchName(idx) {
    return Number(idx) === 0 ? 'Initial point' : `Switch ${Number(idx)}`;
  }

  function renderDashboard() {
    const grid = $('#dashSwitchGrid');
    grid.innerHTML = '';
    if (switchesCache.switches.length === 0) {
      grid.innerHTML = '<div class="empty-state">No switches configured yet — set them up in the Switches tab.</div>';
      return;
    }
    switchesCache.switches.forEach(sw => {
      const card = document.createElement('div');
      card.className = 'switch-card';
      card.innerHTML = `
        <span class="sw-name">${escapeHtml(sw.name || defaultSwitchName(sw.index))}</span>
        <div class="sw-actions">
          <button class="btn btn-on" data-press="on" data-idx="${sw.index}" ${sw.on.saved ? '' : 'disabled'}>ON</button>
          <button class="btn btn-off" data-press="off" data-idx="${sw.index}" ${sw.off.saved ? '' : 'disabled'}>OFF</button>
        </div>
      `;
      grid.appendChild(card);
    });
  }

  $('#dashSwitchGrid').addEventListener('click', async (e) => {
    const btn = e.target.closest('[data-press]');
    if (!btn) return;
    const idx = btn.dataset.idx;
    const action = btn.dataset.press;
    btn.disabled = true;
    try {
      await apiPost(`/api/press/${idx}/${action}`);
      toast(`Pressing ${action.toUpperCase()}…`, 'success');
    } catch (e) { fail(e); }
    finally { setTimeout(() => { btn.disabled = false; }, 1500); }
  });

  $('#btnHomeNow').addEventListener('click', () => triggerHome());
  $('#btnHomeSystem').addEventListener('click', () => triggerHome());
  async function triggerHome() {
    try {
      await apiPost('/api/home');
      toast('Homing started…', 'success');
    } catch (e) { fail(e); }
  }

  $('#btnForceHome')?.addEventListener('click', async () => {
    const ok = confirm(
      'This marks the CURRENT arm position as home without running obstacle detection.\n\n' +
      'Only do this if you have already jogged the arm to the correct physical zero point. Continue?'
    );
    if (!ok) return;
    try {
      await apiPost('/api/home/force');
      toast('Homed manually at current position', 'success');
      await refreshJogReadouts();
    } catch (e) { fail(e); }
  });

  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  }

  // ===================== SWITCHES TAB =====================
  const stepSizes = [0.5, 1, 5];
  const jogStep = {};        // per-switch-index current step size, default 1mm
  const teachState = {};     // per-switch-index selected position: "on" or "off"
  const teachDraft = {};     // per-switch-index temporary ON/OFF X/Y targets

  async function loadSwitchesTab() {
    try {
      await refreshSwitches();
      const sel = $('#switchCount');
      sel.innerHTML = '';
      for (let i = 1; i <= 10; i++) {
        const o = document.createElement('option');
        o.value = i; o.textContent = i;
        if (i === switchesCache.count) o.selected = true;
        sel.appendChild(o);
      }
      renderSwitchEditors();
    } catch (e) { fail(e); }
  }

  $('#btnSaveSwitchCount').addEventListener('click', async () => {
    const count = parseInt($('#switchCount').value, 10);
    try {
      await apiPost('/api/switches', { count, type: switchesCache.type || 0 });
      toast('Switch count saved', 'success');
      await loadSwitchesTab();
    } catch (e) { fail(e); }
  });

  function getSwitchByIndex(idx) {
    return switchesCache.switches.find(sw => String(sw.index) === String(idx));
  }

  function ensureTeachState(idx) {
    if (!teachState[idx]) teachState[idx] = 'on';
    if (!teachDraft[idx]) teachDraft[idx] = { on: null, off: null };
  }

  function posText(pos) {
    return pos && pos.saved ? `X ${Number(pos.x).toFixed(1)}  Y ${Number(pos.y).toFixed(1)}` : 'not saved';
  }

  function draftText(idx, state) {
    ensureTeachState(idx);
    const d = teachDraft[idx][state];
    if (!d) return 'not selected';
    return `X ${Number(d.x).toFixed(1)}  Y ${Number(d.y).toFixed(1)}`;
  }

  function seedTeachDraftFromSwitch(sw) {
    ensureTeachState(sw.index);
    if (sw.on.saved && !teachDraft[sw.index].on) {
      teachDraft[sw.index].on = { x: Number(sw.on.x), y: Number(sw.on.y) };
    }
    if (sw.off.saved && !teachDraft[sw.index].off) {
      teachDraft[sw.index].off = { x: Number(sw.off.x), y: Number(sw.off.y) };
    }
  }

  function setSavedPositionInCache(idx, state, x, y) {
    const sw = getSwitchByIndex(idx);
    if (!sw) return;
    sw[state].saved = true;
    sw[state].x = Number(x);
    sw[state].y = Number(y);
    ensureTeachState(idx);
    teachDraft[idx][state] = { x: Number(x), y: Number(y) };
    updateSwitchCardPositionUI(idx);
  }

  function updateSwitchCardPositionUI(idx) {
    ensureTeachState(idx);
    const card = $(`[data-switch-card="${idx}"]`);
    const sw = getSwitchByIndex(idx);
    if (!card || !sw) return;

    const active = teachState[idx];
    $$('[data-teach]', card).forEach(btn => {
      btn.classList.toggle('active', btn.dataset.teach === active);
    });

    const mode = $('.teach-active-label', card);
    if (mode) mode.textContent = `Editing ${active.toUpperCase()} position`;

    const onValue = $('.pos-saved-value[data-state="on"]', card);
    const offValue = $('.pos-saved-value[data-state="off"]', card);
    if (onValue) onValue.textContent = posText(sw.on);
    if (offValue) offValue.textContent = posText(sw.off);

    const onBox = $('.pos-saved[data-state="on"]', card);
    const offBox = $('.pos-saved[data-state="off"]', card);
    if (onBox) {
      onBox.classList.toggle('has-pos', !!sw.on.saved);
      onBox.classList.toggle('active-teach', active === 'on');
    }
    if (offBox) {
      offBox.classList.toggle('has-pos', !!sw.off.saved);
      offBox.classList.toggle('active-teach', active === 'off');
    }

    const draft = $('.teach-target-value', card);
    if (draft) draft.textContent = draftText(idx, active);

    // Keep the X/Y number inputs in sync with the selected ON/OFF draft,
    // but never overwrite a field the user is actively typing in.
    const d = teachDraft[idx][active];
    const xInput = $('.xy-input[data-axis="x"]', card);
    const yInput = $('.xy-input[data-axis="y"]', card);
    if (xInput && document.activeElement !== xInput) {
      xInput.value = d ? Number(d.x).toFixed(1) : '';
    }
    if (yInput && document.activeElement !== yInput) {
      yInput.value = d ? Number(d.y).toFixed(1) : '';
    }

    const testOn = $('[data-test="on"]', card);
    const testOff = $('[data-test="off"]', card);
    if (testOn) testOn.disabled = !sw.on.saved;
    if (testOff) testOff.disabled = !sw.off.saved;
  }

  async function saveSwitchTarget(idx, state, x, y, showToast = false) {
    await apiPut(`/api/switches/${idx}/position`, { state, x, y });
    setSavedPositionInCache(idx, state, x, y);
    if (showToast) toast(`${state.toUpperCase()} position saved`, 'success');
    renderDashboard();
  }

  async function selectTeachPosition(idx, state) {
    ensureTeachState(idx);
    teachState[idx] = state;
    updateSwitchCardPositionUI(idx);

    const sw = getSwitchByIndex(idx);
    if (!sw) return;

    // If this ON/OFF position already exists, move the motors there so you can edit it.
    if (sw[state].saved) {
      const x = Number(sw[state].x);
      const y = Number(sw[state].y);
      teachDraft[idx][state] = { x, y };
      await apiPost('/api/move', { x, y });
      toast(`Moved to ${sw.name || defaultSwitchName(idx)} ${state.toUpperCase()} position`, 'success');
    } else {
      // If it is new, start from the current real motor position — but
      // still send it through /api/move (a no-op move to the same spot)
      // so the drivers power up right away instead of staying de-energized
      // until the first jog/typed value.
      const pos = await apiGet('/api/position');
      teachDraft[idx][state] = { x: Number(pos.x), y: Number(pos.y) };
      await apiPost('/api/move', { x: pos.x, y: pos.y });
      await saveSwitchTarget(idx, state, pos.x, pos.y, true);
    }
    updateSwitchCardPositionUI(idx);
    await refreshJogReadouts();
  }

  function renderSwitchEditors() {
    const list = $('#switchEditorList');
    list.innerHTML = '';
    switchesCache.switches.forEach(sw => {
      jogStep[sw.index] = jogStep[sw.index] || 1;
      ensureTeachState(sw.index);
      seedTeachDraftFromSwitch(sw);

      const active = teachState[sw.index];
      const card = document.createElement('div');
      card.className = 'card switch-edit-card';
      card.dataset.switchCard = sw.index;
      card.innerHTML = `
        <div class="field">
          <label>Name</label>
          <input class="text-input sw-name-input" data-idx="${sw.index}" type="text"
                 value="${escapeHtml(sw.name)}" placeholder="${defaultSwitchName(sw.index)}">
        </div>

        <div class="teach-selector">
          <span class="teach-active-label">Editing ${active.toUpperCase()} position</span>
          <div class="teach-buttons">
            <button class="step-btn teach-btn ${active === 'on' ? 'active' : ''}" data-teach="on" data-idx="${sw.index}">Edit ON</button>
            <button class="step-btn teach-btn ${active === 'off' ? 'active' : ''}" data-teach="off" data-idx="${sw.index}">Edit OFF</button>
          </div>
        </div>

        <div class="jog-panel">
          <div class="jog-readout" id="jogReadout-${sw.index}">Live X — Y —</div>
          <div class="teach-target-readout">Selected target: <span class="teach-target-value">${draftText(sw.index, active)}</span></div>
          <div class="xy-form">
            <div class="field">
              <label for="xInput-${sw.index}">X (mm)</label>
              <input class="text-input xy-input" id="xInput-${sw.index}" data-axis="x" data-idx="${sw.index}" type="number" step="0.1" inputmode="decimal">
            </div>
            <div class="field">
              <label for="yInput-${sw.index}">Y (mm)</label>
              <input class="text-input xy-input" id="yInput-${sw.index}" data-axis="y" data-idx="${sw.index}" type="number" step="0.1" inputmode="decimal">
            </div>
          </div>
          <div class="jog-grid">
            <span></span>
            <button data-jog="0,-1" data-idx="${sw.index}">▲</button>
            <span></span>
            <button data-jog="-1,0" data-idx="${sw.index}">◀</button>
            <span class="jog-center">JOG</span>
            <button data-jog="1,0" data-idx="${sw.index}">▶</button>
            <span></span>
            <button data-jog="0,1" data-idx="${sw.index}">▼</button>
            <span></span>
          </div>
          <div class="step-picker" data-idx="${sw.index}">
            ${stepSizes.map(s => `<button class="step-btn ${s === jogStep[sw.index] ? 'active' : ''}" data-step="${s}">${s}mm</button>`).join('')}
          </div>
          <div class="teach-help">Type exact X/Y values or use the arrow buttons — both move the real motors live and auto-save the selected ON/OFF position.</div>
        </div>

        <div class="pos-readout-pair">
          <div class="pos-saved ${sw.on.saved ? 'has-pos' : ''} ${active === 'on' ? 'active-teach' : ''}" data-state="on">
            <span class="pos-saved-label">ON position</span>
            <span class="pos-saved-value" data-state="on">${posText(sw.on)}</span>
          </div>
          <div class="pos-saved ${sw.off.saved ? 'has-pos' : ''} ${active === 'off' ? 'active-teach' : ''}" data-state="off">
            <span class="pos-saved-label">OFF position</span>
            <span class="pos-saved-value" data-state="off">${posText(sw.off)}</span>
          </div>
        </div>

        <div class="save-row">
          <button class="btn btn-on" data-save="on" data-idx="${sw.index}">Save current as ON</button>
          <button class="btn btn-off" data-save="off" data-idx="${sw.index}">Save current as OFF</button>
        </div>
        <div class="test-row">
          <button class="btn btn-ghost" data-test="on" data-idx="${sw.index}" ${sw.on.saved ? '' : 'disabled'}>Test ON</button>
          <button class="btn btn-ghost" data-test="off" data-idx="${sw.index}" ${sw.off.saved ? '' : 'disabled'}>Test OFF</button>
        </div>
      `;
      list.appendChild(card);
      updateSwitchCardPositionUI(sw.index);
    });
    refreshJogReadouts();
  }

  async function refreshJogReadouts() {
    try {
      const pos = await apiGet('/api/position');
      $$('.jog-readout').forEach(el => {
        const text = `Live X ${pos.x.toFixed(1)}  Y ${pos.y.toFixed(1)}`;
        if (el.textContent !== text) {
          el.textContent = text;
          el.classList.remove('moving');
          void el.offsetWidth; // trigger reflow
          el.classList.add('moving');
        }
        el.classList.toggle('live', !!pos.moving);
      });
      updateObstacleBanner(pos.obstacle_m1, pos.obstacle_m2);
    } catch (e) { /* ignore */ }
  }

  // The commanded X/Y readout advances from step count alone — it does not
  // confirm the motor physically turned. If a DIAG obstacle latch fired,
  // that motor's driver gets disabled and further jog/typed moves silently
  // stop having any physical effect. Surface that state instead of hiding it.
  function updateObstacleBanner(m1, m2) {
    const banner = $('#obstacleBanner');
    if (!banner) return;
    if (m1 || m2) {
      const which = m1 && m2 ? 'Motor 1 and Motor 2' : (m1 ? 'Motor 1' : 'Motor 2');
      $('#obstacleBannerText').textContent =
        `${which} obstacle-stopped and disabled — jog/typed moves won't physically turn it until reset.`;
      banner.style.display = '';
    } else {
      banner.style.display = 'none';
    }
  }

  $('#btnResetObstacle')?.addEventListener('click', async () => {
    try {
      await apiPost('/api/obstacle/reset');
      toast('Motors re-enabled', 'success');
      await refreshJogReadouts();
    } catch (e) { fail(e); }
  });

  // Typing an exact X/Y value moves the real motors in real time (debounced)
  // and auto-saves it to whichever ON/OFF position is currently selected —
  // same auto-save behavior as the jog buttons, just with typed numbers.
  const xyMoveTimers = {}; // idx -> timeout id
  async function moveFromXYInputs(idx) {
    const card = $(`[data-switch-card="${idx}"]`);
    if (!card) return;
    const xInput = $('.xy-input[data-axis="x"]', card);
    const yInput = $('.xy-input[data-axis="y"]', card);
    if (!xInput || !yInput) return;

    const x = parseFloat(xInput.value);
    const y = parseFloat(yInput.value);
    if (!Number.isFinite(x) || !Number.isFinite(y)) return;

    ensureTeachState(idx);
    const state = teachState[idx];

    try {
      await apiPost('/api/move', { x, y });
      await saveSwitchTarget(idx, state, x, y, false);
      updateSwitchCardPositionUI(idx);
      await refreshJogReadouts();
    } catch (e) { fail(e); }
  }

  $('#switchEditorList').addEventListener('input', (e) => {
    const input = e.target.closest('.xy-input');
    if (!input) return;
    const idx = input.dataset.idx;
    clearTimeout(xyMoveTimers[idx]);
    xyMoveTimers[idx] = setTimeout(() => moveFromXYInputs(idx), 350);
  });

  $('#switchEditorList').addEventListener('blur', async (e) => {
    const input = e.target.closest('.sw-name-input');
    if (!input) return;
    try {
      await apiPut(`/api/switches/${input.dataset.idx}`, { name: input.value });
      const sw = getSwitchByIndex(input.dataset.idx);
      if (sw) sw.name = input.value || defaultSwitchName(input.dataset.idx);
      toast('Name saved', 'success');
      renderDashboard();
    } catch (e) { fail(e); }
  }, true);

  $('#switchEditorList').addEventListener('click', async (e) => {
    const teachBtn = e.target.closest('[data-teach]');
    if (teachBtn) {
      teachBtn.disabled = true;
      try {
        await selectTeachPosition(teachBtn.dataset.idx, teachBtn.dataset.teach);
      } catch (e) { fail(e); }
      finally { teachBtn.disabled = false; }
      return;
    }

    const stepBtn = e.target.closest('.step-picker .step-btn');
    if (stepBtn) {
      const wrap = stepBtn.closest('.step-picker');
      const idx = wrap.dataset.idx;
      jogStep[idx] = parseFloat(stepBtn.dataset.step);
      $$('.step-btn', wrap).forEach(b => b.classList.toggle('active', b === stepBtn));
      return;
    }

    const jogBtn = e.target.closest('[data-jog]');
    if (jogBtn) {
      const [dxSign, dySign] = jogBtn.dataset.jog.split(',').map(Number);
      const idx = jogBtn.dataset.idx;
      ensureTeachState(idx);
      const state = teachState[idx];
      const step = jogStep[idx] || 1;

      let draft = teachDraft[idx][state];
      if (!draft) {
        try {
          const pos = await apiGet('/api/position');
          draft = teachDraft[idx][state] = { x: Number(pos.x), y: Number(pos.y) };
        } catch (e) { fail(e); return; }
      }

      const x = Number((draft.x + dxSign * step).toFixed(3));
      const y = Number((draft.y + dySign * step).toFixed(3));

      jogBtn.disabled = true;
      try {
        await apiPost('/api/move', { x, y });
        await saveSwitchTarget(idx, state, x, y, false);
        updateSwitchCardPositionUI(idx);
        await refreshJogReadouts();
      } catch (e) { fail(e); }
      finally { jogBtn.disabled = false; }
      return;
    }

    const saveBtn = e.target.closest('[data-save]');
    if (saveBtn) {
      const idx = saveBtn.dataset.idx;
      const state = saveBtn.dataset.save;
      try {
        const pos = await apiGet('/api/position');
        await saveSwitchTarget(idx, state, pos.x, pos.y, true);
        teachState[idx] = state;
        updateSwitchCardPositionUI(idx);
      } catch (e) { fail(e); }
      return;
    }

    const testBtn = e.target.closest('[data-test]');
    if (testBtn) {
      const idx = testBtn.dataset.idx;
      const action = testBtn.dataset.test;
      testBtn.disabled = true;
      try {
        await apiPost(`/api/press/${idx}/${action}`);
        teachState[idx] = action;
        const sw = getSwitchByIndex(idx);
        if (sw && sw[action].saved) {
          teachDraft[idx][action] = { x: Number(sw[action].x), y: Number(sw[action].y) };
        }
        updateSwitchCardPositionUI(idx);
        toast(`Testing ${action.toUpperCase()}…`, 'success');
        setTimeout(refreshJogReadouts, 1200);
      } catch (e) { fail(e); }
      finally { setTimeout(() => { testBtn.disabled = false; }, 1500); }
      return;
    }
  });

  // ===================== SCHEDULES TAB =====================
  let schedulesCache = [];

  async function loadSchedulesTab() {
    try {
      await refreshSwitches();
      const sel = $('#schSwitch');
      sel.innerHTML = switchesCache.switches.map(sw =>
        `<option value="${sw.index}">${escapeHtml(sw.name || `Switch ${sw.index + 1}`)}</option>`
      ).join('') || '<option value="0">No switches configured</option>';

      const data = await apiGet('/api/schedules');
      schedulesCache = data.schedules;
      renderSchedules();
    } catch (e) { fail(e); }
  }

  const dayLetters = ['S', 'M', 'T', 'W', 'T', 'F', 'S'];
  function describeDays(mask) {
    if (mask === 0x7F) return 'Every day';
    const days = [];
    for (let i = 0; i < 7; i++) if (mask & (1 << i)) days.push(dayLetters[i]);
    return days.join(' ') || 'No days';
  }

  function switchName(idx) {
    const sw = switchesCache.switches.find(s => s.index === idx);
    return sw ? (sw.name || `Switch ${idx + 1}`) : `Switch ${idx + 1}`;
  }

  function renderSchedules() {
    const wrap = $('#scheduleList');
    if (schedulesCache.length === 0) {
      wrap.innerHTML = '<div class="empty-state">No schedules yet — add one below.</div>';
      return;
    }
    wrap.innerHTML = schedulesCache.map(s => {
      const time = `${String(s.hour).padStart(2, '0')}:${String(s.minute).padStart(2, '0')}`;
      let sub = time;
      if (s.type === 'daily') sub += ' · Every day';
      else if (s.type === 'days_of_week') sub += ' · ' + describeDays(s.days_mask);
      else sub += ` · ${s.date}`;
      const firedBadge = (s.type === 'one_time' && !s.enabled) ? '<span class="badge">Fired</span>' : '';
      return `
        <div class="sched-row">
          <div class="sched-main">
            <span class="sched-title">${escapeHtml(switchName(s.switch_index))} → ${s.action.toUpperCase()}</span>
            <span class="sched-sub">${sub}</span>
          </div>
          <div class="sched-actions">
            ${firedBadge}
            <label class="step-btn ${s.enabled ? 'active' : ''}" style="cursor:pointer;">
              <input type="checkbox" data-toggle="${s.index}" ${s.enabled ? 'checked' : ''} style="display:none;">
              ${s.enabled ? 'On' : 'Off'}
            </label>
            <button class="icon-btn" data-delete="${s.index}" title="Delete">✕</button>
          </div>
        </div>
      `;
    }).join('');
  }

  $('#scheduleList').addEventListener('click', async (e) => {
    const del = e.target.closest('[data-delete]');
    if (del) {
      try {
        await apiDelete(`/api/schedules/${del.dataset.delete}`);
        toast('Schedule deleted', 'success');
        await loadSchedulesTab();
      } catch (e) { fail(e); }
      return;
    }
    const toggleLabel = e.target.closest('[data-toggle], label.step-btn');
    if (toggleLabel) {
      const cb = toggleLabel.querySelector('input[type=checkbox]');
      if (!cb) return;
      const idx = cb.dataset.toggle;
      const sched = schedulesCache.find(s => String(s.index) === idx);
      if (!sched) return;
      try {
        await apiPut(`/api/schedules/${idx}`, {
          switch_index: sched.switch_index, action: sched.action, type: sched.type,
          hour: sched.hour, minute: sched.minute, days_mask: sched.days_mask,
          date: sched.date, enabled: !sched.enabled,
        });
        await loadSchedulesTab();
      } catch (e) { fail(e); }
    }
  });

  // Add-schedule form interactivity
  $('#schAction').addEventListener('click', (e) => {
    const btn = e.target.closest('.seg-btn');
    if (!btn) return;
    $$('.seg-btn', $('#schAction')).forEach(b => b.classList.toggle('active', b === btn));
  });

  $('#schType').addEventListener('change', () => {
    const v = $('#schType').value;
    $('#schDaysField').style.display = v === 'days_of_week' ? '' : 'none';
    $('#schDateField').style.display = v === 'one_time' ? '' : 'none';
  });

  $('#schDays').addEventListener('click', (e) => {
    const btn = e.target.closest('.day-btn');
    if (btn) btn.classList.toggle('active');
  });

  $('#btnAddSchedule').addEventListener('click', async () => {
    const switchIndex = parseInt($('#schSwitch').value, 10);
    const action = $('#schAction .seg-btn.active').dataset.value;
    const type = $('#schType').value;
    const [hour, minute] = $('#schTime').value.split(':').map(Number);
    let daysMask = 0x7F;
    if (type === 'days_of_week') {
      daysMask = $$('#schDays .day-btn.active').reduce((m, b) => m | parseInt(b.dataset.bit, 10), 0) || 0x7F;
    }
    const date = $('#schDate').value;

    try {
      await apiPost('/api/schedules', {
        switch_index: switchIndex, action, type, hour, minute, days_mask: daysMask, date, enabled: true,
      });
      toast('Schedule added', 'success');
      await loadSchedulesTab();
    } catch (e) { fail(e); }
  });

  // ===================== SETTINGS TAB =====================
  async function loadSettingsTab() {
    try {
      const wifi = await apiGet('/api/wifi');
      $('#staSsid').value = wifi.sta_ssid || '';
      $('#apSsid').value = wifi.ap_ssid || '';
      $('#staStatus').textContent = wifi.sta_connected ? `Connected · ${wifi.sta_ip}` : 'Not connected';
      $('#apStatus').textContent = `AP active · ${wifi.ap_ip}`;

      const time = await apiGet('/api/time');
      $('#ntpServer').value = time.ntp_server || '';
      $('#tzInput').value = time.tz || '';
      $('#timeNow').textContent = time.now || '—';
    } catch (e) { fail(e); }
  }

  $('#btnConnectSta').addEventListener('click', async () => {
    try {
      await apiPost('/api/wifi/sta', { ssid: $('#staSsid').value, pass: $('#staPass').value });
      toast('Connecting…', 'success');
    } catch (e) { fail(e); }
  });

  $('#btnSaveAp').addEventListener('click', async () => {
    try {
      await apiPost('/api/wifi/ap', { ssid: $('#apSsid').value, pass: $('#apPass').value });
      toast('Access point updated — you may be disconnected briefly', 'success');
    } catch (e) { fail(e); }
  });

  $('#btnSaveTime').addEventListener('click', async () => {
    try {
      await apiPost('/api/time', { tz: $('#tzInput').value, ntp_server: $('#ntpServer').value });
      toast('Time settings saved', 'success');
      await loadSettingsTab();
    } catch (e) { fail(e); }
  });

  // -------------------------------------------------------------
  // Boot
  // -------------------------------------------------------------
  const initialTab = (location.hash || '#dashboard').slice(1);
  showTab(['dashboard', 'switches', 'schedules', 'settings'].includes(initialTab) ? initialTab : 'dashboard');
  loadDashboard();
})();