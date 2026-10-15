'use strict';
// KWL-Simulator - Oberflaeche. Holt Metadaten einmal, fragt den Zustand alle
// 250 ms ab und zeichnet nur, was sich geaendert hat. Eingaben gehen als
// Aktionen an die Python-App, die sie an den Kern weiterreicht.

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const h = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'html') e.innerHTML = v;
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat()) if (kid !== null && kid !== undefined && kid !== false)
    e.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  return e;
};
const de = (v, d = 2) => (v === null || v === undefined || Number.isNaN(v)) ? '—'
  : Number(v).toLocaleString('de-DE', { minimumFractionDigits: d, maximumFractionDigits: d });
const fmtT = ms => {
  const s = Math.floor(ms / 1000);
  return `T+${String(Math.floor(s / 3600)).padStart(2, '0')}:${String(Math.floor(s / 60) % 60).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
};
const fmtShortT = ms => {
  const s = ms / 1000;
  if (s < 120) return de(s, 1) + ' s';
  if (s < 7200) return de(s / 60, 1) + ' min';
  return de(s / 3600, 2) + ' h';
};

const ERR = {
  0: 'kein Fehler', 1: 'Freigabe fehlt', 2: 'Master-Timeout', 3: 'Konfiguration', 4: 'DAC nicht erreichbar',
  5: 'ungültiger Empfangswert', 6: 'suspendiert', 7: 'Feuchtevergleich sperrt', 8: 'Messwerte fehlen',
  9: 'Schutzbetrieb', 10: 'Filterwechsel fällig', 11: 'Richtungskonflikt',
};
const isAlarm = c => c >= 1 && c <= 5;
const HVAC = [[0, 'Auto'], [1, 'Komfort'], [2, 'Standby'], [3, 'Nacht/Eco'], [4, 'Frost-/Gebäudeschutz'],
  [5, 'Stoßlüften'], [6, 'Temperatur-Absenkung'], [7, 'Ruhe (Aus)']];
const CH_COLORS = ['--s1', '--s2', '--s3', '--s4'];

const S = {
  meta: null, params: {}, kos: {}, pages: [],
  poll: null, since: -1, dacIdx: 0, kv: -1,
  dac: [], events: [], kovals: {},
  tab: 'inputs', page: null, win: 600000, inRoom: (() => { try { return localStorage.getItem('kwlsim.inRoom'); } catch (e) { return null; } })(),
  envHist: [], envIdx: 0, envQ: 'T', envWin: 21600000, sigEnv: '',
  values: {}, activeValues: {}, visKos: new Map(),
  logLines: [], busRows: [], checks: [], warnCount: 0,
  sigInputs: '', sigParams: '', sigKos: '',
};

// ------------------------------------------------------------ Server
async function act(body) {
  const r = await fetch('/api/action', { method: 'POST', body: JSON.stringify(body) });
  const j = await r.json();
  if (!j.ok) alert('Fehler: ' + j.error);
  setTimeout(poll, 30);
  return j;
}

// ------------------------------------------------------------ ETS-Sichtbarkeit
function evalTest(v, test) {
  test = String(test).trim();
  const m = test.match(/^(<=|>=|!=|<|>|=)\s*(-?[\d.]+)$/);
  const x = Number(v);
  if (m) {
    const y = Number(m[2]);
    switch (m[1]) {
      case '<': return x < y; case '>': return x > y; case '<=': return x <= y;
      case '>=': return x >= y; case '!=': return x != y; default: return x == y;
    }
  }
  return test.split(/\s+/).some(t => Number(t) === x || t === String(v));
}

function condOk(conds, vals) {
  for (const c of conds) {
    const v = vals[c[0]];
    if (c[1] === null || c[1] === undefined) {
      if ((c[2] || []).some(t => evalTest(v, t))) return false;
    } else if (!evalTest(v, c[1])) return false;
  }
  return true;
}

/// Werte wie die ETS sie sieht: Vorgaben, darueber die Projektwerte, dann die
/// <Assign>-Zuweisungen (z. B. Kanalzahl aus der Platinenwahl).
function effectiveValues(changed) {
  const v = {};
  for (const [n, p] of Object.entries(S.params)) v[n] = p.d;
  Object.assign(v, changed);
  for (let pass = 0; pass < 2; pass++)
    for (const pg of S.pages) {
      if (!condOk(pg.cond, v)) continue;
      for (const it of pg.items)
        if (it.type === 'a' && it.target && condOk(it.cond, v))
          v[it.target] = it.source ? v[it.source] : Number(it.value);
    }
  return v;
}

function visibleKos(vals) {
  const m = new Map();
  for (const pg of S.pages)
    for (const it of pg.items)
      if (it.type === 'k' && !m.has(it.nr) && condOk(it.cond, vals)) {
        m.set(it.nr, it.dpt);
        // Text der sichtbaren Referenz, wie in der ETS ("Automatik aktiv" statt
        // "Handbetrieb aktiv", wenn die Polaritaet umgestellt ist).
        const k = S.kos[it.nr];
        if (k) {
          if (k.t0 === undefined) { k.t0 = k.t; k.f0 = k.f; }
          k.t = it.t || k.t0; k.f = it.f || k.f0;
        }
      }
  return m;
}

// ------------------------------------------------------------ Abfrage
async function poll() {
  let j;
  try {
    const r = await fetch(`/api/poll?since=${S.since}&dac=${S.dacIdx}&kv=${S.kv}&eh=${S.envIdx}`);
    j = await r.json();
  } catch (e) { return; }
  S.poll = j;
  S.since = j.seq;
  if (j.dacIdx < S.dacIdx) S.dac = [];   // Kern neu gestartet
  S.dac.push(...j.dac);
  S.dacIdx = j.dacIdx;
  if (j.kos) { S.kovals = j.kos; S.kv = j.kosVersion; }
  if (j.env) {
    if (j.env.histIdx < S.envIdx) S.envHist = [];     // Projekt neu geladen
    S.envHist.push(...j.env.hist);
    S.envIdx = j.env.histIdx;
  }
  for (const ev of j.events) onEvent(ev);

  S.values = effectiveValues(j.pending);
  S.activeValues = effectiveValues(j.active);
  S.visKos = visibleKos(S.activeValues);
  render();
}

function onEvent(ev) {
  const t = ev.t !== undefined ? fmtT(ev.t) : '';
  switch (ev.ev) {
    case 'log':
      pushLog(`${t}  ${ev.src ? ev.src + ': ' : ''}${ev.text}`, ev.level); break;
    case 'ko': case 'rx':
      S.busRows.push(ev);
      if (S.busRows.length > 3000) S.busRows.splice(0, 1000);
      if (ev.ev === 'ko' && ev.nr === 7) pushLog(`${t}  Diagnose-KO: ${ev.val}`, 'diag');
      break;
    case 'warn':
      S.checks.push({ t: ev.t, kind: ev.inv, text: ev.text + (ev.count > 1 ? ` (${ev.count}×)` : ''), cls: 'err' });
      S.warnCount++; break;
    case 'info':
      S.checks.push({ t: ev.t, kind: 'Hinweis', text: ev.text, cls: 'wrn' }); break;
    case 'error':
      S.checks.push({ t: ev.t, kind: 'Fehler', text: ev.text, cls: 'err' });
      pushLog(`${t}  FEHLER: ${ev.text}`, 'error'); break;
    case 'boot':
      pushLog(`${t}  ── Neustart, ${ev.configured ? 'parametriert' : 'ohne Parametrierung'} ──`, 'debug');
      S.checks.push({ t: ev.t, kind: 'Start', text: ev.configured ? 'Neustart mit Parametern' : 'Neustart ohne Parameter', cls: 'muted' });
      break;
    case 'download':
      pushLog(`${t}  ETS-Download ${ev.phase === 'start' ? 'beginnt' : 'fertig'}`, 'debug'); break;
    case 'v12':
      S.checks.push({ t: ev.t, kind: '12 V', text: ev.on ? '12 V ein (DACs im Werkszustand)' : '12 V aus', cls: 'muted' }); break;
    case 'mcu':
      S.checks.push({ t: ev.t, kind: 'KNX-Bus', text: 'Bus aus, Prozessor aus', cls: 'muted' }); break;
  }
  if (S.checks.length > 2000) S.checks.splice(0, 500);
}

function pushLog(text, cls = '') {
  S.logLines.push({ text, cls });
  if (S.logLines.length > 3000) S.logLines.splice(0, 1000);
  S.logDirty = true;
}

// ------------------------------------------------------------ Kopfleiste
function renderBar() {
  const st = S.poll.state || {};
  $('#clock').textContent = fmtT(st.t || 0);
  $('#uptime').textContent = st.mcu ? 'Laufzeit ' + fmtShortT(st.uptime || 0) : 'Prozessor aus';
  $$('#speed [data-speed]').forEach(b => b.classList.toggle('active', Number(b.dataset.speed) === S.poll.speed));
  for (const [id, on] of [['#pwr-bus', st.bus], ['#pwr-v12', st.v12]]) {
    $(id).classList.toggle('on', !!on); $(id).classList.toggle('off', !on);
  }
  const dirty = Object.keys(diffValues()).length;
  $('#btn-program').classList.toggle('dirty', dirty > 0);
  $('#btn-program').textContent = dirty ? `Programmieren (${dirty})` : 'Programmieren';
  $('#dirty-count').textContent = dirty || '';
  $('#warn-count').textContent = S.warnCount || '';

  const b = [];
  const badge = (txt, cls, tip) => b.push(h('span', { class: 'badge ' + cls, title: tip }, txt));
  badge(st.mcu ? 'Prozessor läuft' : 'Prozessor aus', st.mcu ? 'ok' : 'bad');
  badge(st.paramsValid ? 'programmiert' : 'nie programmiert', st.paramsValid ? 'ok' : 'warn');
  if (st.downloading) badge('ETS-Download', 'warn');
  const m = st.sim?.module;
  if (m?.error) badge(`Fehler ${m.error}: ${ERR[m.error]}`, 'bad');
  if (m?.powerSaved) badge('SAVE aktiv', 'warn');
  $('#badges').replaceChildren(...b);

  const led = st.sim?.led || {};
  const el = $('#led');
  el.className = 'led' + (led.mode === 'an' ? ' on' : led.mode === 'blinkt' ? ' blink' : led.mode === 'Blinkcode' ? ' code' : '');
  el.dataset.code = led.code || '';
  el.title = led.func ? `Info-LED, Funktion ${led.func}: ${led.mode}${led.code ? ' ' + led.code : ''}` : 'Info-LED: keine Lüftungsfunktion zugewiesen';

  const sel = $('#project');
  const names = S.poll.projects.join('|') + '#' + S.poll.project;
  if (sel.dataset.sig !== names) {
    sel.dataset.sig = names;
    sel.replaceChildren(h('option', { value: '' }, '— Projekt laden —'),
      ...S.poll.projects.map(p => h('option', { value: p, selected: p === S.poll.project }, p)));
  }
}

function diffValues() {
  const p = S.poll.pending || {}, a = S.poll.active || {}, d = {};
  for (const k of new Set([...Object.keys(p), ...Object.keys(a)]))
    if (p[k] !== a[k]) d[k] = p[k];
  return d;
}

// ------------------------------------------------------------ Ausgaenge
function channelFans(fans) {
  const map = {};
  for (const f of fans || [])
    for (let k = 0; k < f.channels; k++) map[f.ch + k] = { fan: f, motor: f.channels > 1 ? k + 1 : 0 };
  return map;
}

function renderOutputs() {
  const st = S.poll.state; if (!st?.sim) return;
  const map = channelFans(st.sim.fans);
  const cards = st.sim.dac.map((d, i) => {
    const ch = i + 1, cf = map[ch], f = cf?.fan;
    const bip = f ? f.bipolar : true;
    const v = d.volt;
    let what;
    if (!d.powered) what = h('span', { class: 'muted' }, 'keine 12 V');
    else if (!f) what = h('span', { class: 'muted' }, Math.abs(v - 5) < 0.05 ? 'kein Lüfter · 5,00 V' : 'kein Lüfter zugeordnet');
    else {
      const pct = bip ? Math.abs(v - 5) / 5 * 100 : v / 10 * 100;
      const still = bip ? Math.abs(v - 5) < 0.05 : v < 0.05;
      what = h('span', {},
        h('b', {}, `Lüfter ${f.no}`), ` ${f.type}${cf.motor ? ' M' + cf.motor : ''} · Raum ${f.room}`, h('br'),
        still ? h('span', { class: 'muted' }, 'Stillstand')
          : h('span', { class: 'dir-' + f.dir }, `Stufe ${f.stage} ${f.dir}`, h('span', { class: 'muted' }, ` · ${de(pct, 0)} %`)));
      if (bip && v < 0.3) what.append(h('br'), h('span', { class: 'err' }, 'Volllast-Signal!'));
    }
    const pos = Math.max(0, Math.min(100, v * 10));
    return h('div', { class: 'out' + (d.powered ? '' : ' dead'), style: `--c: var(${CH_COLORS[i]})` },
      h('div', { class: 'ch' }, h('b', {}, 'S' + ch), h('span', { class: 'mono', title: 'Registerwert auf dem Bus' }, '0x' + d.wire.toString(16).toUpperCase().padStart(4, '0'))),
      h('div', { class: 'v' }, de(v, 2), h('small', {}, ' V')),
      h('div', { class: 'meter' + (bip ? '' : ' uni') }, bip ? h('span', { class: 'mid' }) : null, h('span', { class: 'pos', style: `left:${pos}%` })),
      h('div', { class: 'what' }, what));
  });
  $('#outputs').replaceChildren(...cards);

  // Luefter
  const fans = st.sim.fans;
  $('#fans').replaceChildren(
    h('tr', {}, ...['Lüfter', 'Typ', 'Kanal', 'Raum', 'Takt', 'Stufe', 'Richtung', 'WRG', 'Soll-U', 'Fehler'].map(x => h('th', {}, x))),
    ...(fans.length ? fans.map(f => h('tr', {},
      h('td', {}, 'L' + f.no), h('td', {}, f.type),
      h('td', {}, 'S' + f.ch + (f.channels > 1 ? '/S' + (f.ch + 1) : '')),
      h('td', {}, f.room),
      h('td', {}, f.group ? `V${f.group}` : 'eigen', h('span', { class: 'muted' }, ` · Ph ${f.phase ? 'B' : 'A'}${f.share < 100 ? ' · ' + f.share + ' %' : ''}`)),
      h('td', { class: 'num' }, f.stage),
      h('td', { class: 'dir-' + f.dir }, f.stage ? f.dir : '—'),
      h('td', {}, f.hrv ? 'ja' : '—'),
      h('td', { class: 'num' }, de(f.volt, 2)),
      h('td', { class: f.err ? (isAlarm(f.err) ? 'err' : 'wrn') : 'muted' }, f.err ? `${f.err} ${ERR[f.err]}` : '—'),
    )) : [h('tr', {}, h('td', { colspan: 10, class: 'muted' }, 'kein Lüfter aktiv'))]));

  // Raeume
  const rooms = st.sim.rooms;
  const sens = r => [r.humIn !== null ? `rF ${de(r.humIn, 0)} %` : null, r.tempIn !== null ? `${de(r.tempIn, 1)} °C` : null,
    r.co2 !== null ? `CO₂ ${de(r.co2, 0)}` : null, r.voc !== null ? `VOC ${de(r.voc, 0)}` : null].filter(Boolean).join(' · ') || '—';
  $('#rooms').replaceChildren(
    h('tr', {}, ...['Raum', 'Stufe', 'Herkunft', 'Betriebsart', 'Richtung', 'Deckel', 'Abluft', 'Messwerte', 'Meldung'].map(x => h('th', {}, x))),
    ...(rooms.length ? rooms.map(r => h('tr', {},
      h('td', { class: 'nw' }, 'R' + r.no), h('td', { class: 'num' }, r.stage),
      h('td', { class: 'nw', title: 'Rang, aus dem die Stufe kommt' }, r.source),
      h('td', { class: 'nw', title: `Betriebsart, gesetzt auf Rang ${r.modeRank}` }, r.mode, h('sup', { class: 'muted' }, ' R' + r.modeRank)),
      h('td', { class: 'nw dir-' + r.dir }, r.dir + (r.cycle === 'Sommer' ? ' · Sommer' : '')),
      h('td', { class: 'num' }, r.cap),
      h('td', {}, r.exhaust + (r.supplyReq ? ' · Zuluft angef.' : '')),
      h('td', { class: 'muted' }, sens(r)),
      h('td', { class: r.err ? 'wrn' : 'muted' }, [r.err ? `${r.err} ${ERR[r.err]}` : null, r.locked ? 'Sperre' : null,
        r.protection ? 'Schutz' : null, r.interval ? null : 'Intervallpause'].filter(Boolean).join(' · ') || '—'),
    )) : [h('tr', {}, h('td', { colspan: 9, class: 'muted' }, 'kein Raum aktiv'))]));

  // Takt
  const groups = st.sim.groups;
  $('#groups').replaceChildren(
    h('tr', {}, ...['Takt', 'Rolle', 'Mitgl.', 'Stufe', 'Phase A', 'Zyklus', 'seit', 'Rampe', 'Totzeit', 'WRG', 'Hinweis'].map(x => h('th', {}, x))),
    ...(groups.length ? groups.map(g => h('tr', {},
      h('td', {}, g.name), h('td', {}, g.role), h('td', { class: 'num' }, g.members),
      h('td', { class: 'num' }, g.stage),
      h('td', { class: 'dir-' + g.dir }, g.dir + (g.fixed ? ' (fest)' : '')),
      h('td', { class: 'num' }, g.cycle + ' s'),
      h('td', { class: 'num' }, de(g.since, 0) + ' s'),
      h('td', {}, g.ramp < 0.999 && !g.inDeadTime ? h('b', {}, `${de(g.ramp * 100, 0)} %`) : g.rampTime + ' s'),
      h('td', {}, g.inDeadTime ? h('b', {}, 'läuft') : g.deadTime + ' s'),
      h('td', {}, g.hrv ? 'ja' : '—'),
      h('td', { class: g.conflict || g.timedOut ? 'err' : 'muted' },
        [g.conflict ? `Konflikt Raum ${g.conflictRoom}` : null, g.timedOut ? 'Master fehlt' : null].filter(Boolean).join(' · ') || '—'),
    )) : [h('tr', {}, h('td', { colspan: 11, class: 'muted' }, 'kein Takt aktiv'))]));

  const m = st.sim.module;
  $('#module').textContent = `I²C-Frames ${m.i2cFrames.toLocaleString('de-DE')} · ohne ACK ${m.i2cNacks} · Ausgabe ${m.outputReady ? 'bereit' : 'NICHT bereit'} · Modulfehler ${m.error} (${ERR[m.error]})`;
}

// ------------------------------------------------------------ Verlauf
function renderChart() {
  const c = $('#chart');
  const dpr = window.devicePixelRatio || 1;
  const w = c.clientWidth, hgt = c.clientHeight || 220;
  if (!w) return;
  // Die Pixelgroesse folgt der CSS-Groesse, nie umgekehrt - sonst waechst der
  // Canvas bei Bildschirmskalierung mit jedem Durchlauf.
  if (c.width !== Math.round(w * dpr) || c.height !== Math.round(hgt * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(hgt * dpr); }
  const g = c.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  g.clearRect(0, 0, w, hgt);
  const L = 34, R = 8, T = 8, B = 20, pw = w - L - R, ph = hgt - T - B;
  const now = S.poll.state?.t || 0, t0 = Math.max(0, now - S.win);
  const X = t => L + (t - t0) / Math.max(1, now - t0) * pw;
  const Y = v => T + (1 - v / 10) * ph;
  g.font = '11px system-ui'; g.fillStyle = css('--muted'); g.strokeStyle = css('--line'); g.lineWidth = 1;
  for (const v of [0, 2.5, 5, 7.5, 10]) {
    g.beginPath(); g.setLineDash(v === 5 ? [4, 3] : []); g.strokeStyle = v === 5 ? css('--muted') : css('--line');
    g.moveTo(L, Y(v)); g.lineTo(L + pw, Y(v)); g.stroke();
    g.fillText(de(v, v % 1 ? 1 : 0), 4, Y(v) + 4);
  }
  g.setLineDash([]);
  g.fillText(fmtT(t0), L, hgt - 5);
  const lbl = fmtT(now); g.fillText(lbl, L + pw - g.measureText(lbl).width, hgt - 5);

  const n = S.poll.state?.sim?.dac?.length || 4;
  let start = 0;
  { let lo = 0, hi = S.dac.length - 1;
    while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (S.dac[mid][0] <= t0) lo = mid; else hi = mid - 1; }
    start = lo; }
  for (let ch = 0; ch < n; ch++) {
    g.strokeStyle = css(CH_COLORS[ch]); g.lineWidth = 1.6; g.beginPath();
    let prev = null;
    for (let i = start; i < S.dac.length; i++) {
      const p = S.dac[i], x = X(Math.max(p[0], t0)), y = Y(p[ch + 1]);
      if (prev === null) g.moveTo(x, y); else { g.lineTo(x, prev); g.lineTo(x, y); }
      prev = y;
    }
    if (prev !== null) g.lineTo(L + pw, prev);
    g.stroke();
  }
  // Legende
  let lx = L + 6;
  for (let ch = 0; ch < n; ch++) {
    g.fillStyle = css(CH_COLORS[ch]); g.fillRect(lx, T + 2, 10, 3);
    g.fillStyle = css('--text'); g.fillText('S' + (ch + 1), lx + 13, T + 7); lx += 38;
  }
}

// ------------------------------------------------------------ Bedienung
function channelOfKo(k) {
  let m = k.name.match(/^ROOM_(\d+)/); if (m) return 'Raum ' + m[1];
  m = k.name.match(/^FAN_(\d+)/); if (m) return 'Lüfter ' + m[1];
  return 'Gerät';
}
/// "Raum 1" -> "Raum 1 · Wohnzimmer", wenn in der ETS ein Name steht.
function chName(ch) {
  const [kind, no] = ch.split(' ');
  const p = kind === 'Raum' ? `ROOM_r${no}Name` : kind === 'Lüfter' ? `FAN_f${no}Name` : null;
  const n = p && S.activeValues[p];
  return n && String(n).trim() ? `${ch} · ${n}` : ch;
}
const chRank = s => (s.startsWith('Raum') ? 0 : s.startsWith('Lüfter') ? 100 : 200) + Number(s.split(' ')[1] || 0);

function koLabel(k) { return (k.t || k.name).replace(/^(Raum|Lüfter) \d+:\s*/, ''); }

/// Abschnitte der Bedienung, nach dem Objektnamen ohne Kanalpraefix.
const SECTION_OF = {
  Mode: 'Betriebsart', ForcedMode: 'Betriebsart', Night: 'Betriebsart', Force1: 'Betriebsart', Force2: 'Betriebsart', Force3: 'Betriebsart',
  StageSet: 'Handstufe', StageSetPct: 'Handstufe', StageStep: 'Handstufe', ManualAct: 'Handstufe',
  Summer: 'Bedienung', DirMode: 'Bedienung', ExhaustReq: 'Bedienung', Lock: 'Bedienung',
  HumIn: 'Messwerte', TempIn: 'Messwerte', HumOut: 'Messwerte', TempOut: 'Messwerte', CO2: 'Messwerte', VOC: 'Messwerte', VOCByte: 'Messwerte', TempSet: 'Messwerte',
  TempLead: 'Führungen freigeben', HumLead: 'Führungen freigeben', CO2Lead: 'Führungen freigeben', VOCLead: 'Führungen freigeben',
  Enable: 'Freigabe und Filter', Suspend: 'Freigabe und Filter', FilterAck: 'Freigabe und Filter', FilterDue: 'Freigabe und Filter',
  GroupStage: 'Verbund-Eingänge (nur Slave)', GroupTact: 'Verbund-Eingänge (nur Slave)', GroupAlive: 'Verbund-Eingänge (nur Slave)', GroupMode: 'Verbund-Eingänge (nur Slave)',
};
const SECTION_ORDER = ['Betriebsart', 'Handstufe', 'Bedienung', 'Messwerte', 'Führungen freigeben', 'Freigabe und Filter', 'Verbund-Eingänge (nur Slave)', 'Weitere'];
const sectionOf = k => SECTION_OF[k.name.replace(/^(ROOM|FAN)_\d+/, '')] || 'Weitere';

/// Wert eines Objekts lesbar, mit Einheit nach DPT.
function fmtKo(nr, val, dpt) {
  if (val === undefined || val === null) return '—';
  const k = S.kos[nr] || { name: '' };
  const [m, s] = String(dpt || '').split('.').map(Number);
  if (m === 20) return (HVAC.find(x => x[0] === val) || [0, String(val)])[1];
  if (typeof val === 'boolean') return val ? '1' : '0';
  if (typeof val !== 'number') return String(val);
  if (m === 9) {
    if (s === 1) return de(val, 1) + ' °C';
    if (s === 7) return de(val, 1) + ' %';
    if (s === 8) return de(val, 0) + (/VOC/.test(k.name) ? ' ppb' : ' ppm');
    if (s === 20) return de(val / 1000, 2) + ' V';
    if (s === 9) return de(val, 0) + ' m³/h';
    if (s === 29) return de(val, 1) + ' g/kg';
    return de(val, 1);
  }
  if (m === 5 && s === 1) return de(val, 0) + ' %';
  if (m === 7 && s === 7) return de(val, 0) + ' h';
  return de(val, 0);
}
function koValText(nr, dpt) { return fmtKo(nr, S.kovals[nr]?.val, dpt); }

/// Objekte, die die Umgebungssimulation gerade fuehrt.
function envDriven() {
  const e = S.poll?.env, out = new Set();
  if (!e?.config.enabled) return out;
  const key = { TempIn: 'temp', HumIn: 'hum', CO2: 'co2', VOC: 'voc', TempOut: 'out', HumOut: 'out', TempSet: 'set' };
  for (const [no, m] of Object.entries(e.targets || {}))
    for (const [q, nr] of Object.entries(m))
      if ((e.config.rooms[no]?.send || {})[key[q]]) out.add(nr);
  return out;
}

function sendKo(nr, dpt, value, cyclic) { return act({ action: 'ko', nr, dpt, value, cyclic: cyclic || 0 }); }

function cycPeriod() {
  try { return Number(localStorage.getItem('kwlsim.cyc')) || 60; } catch (e) { return 60; }
}

function inputControl(k, dpt) {
  const main = Number(dpt.split('.')[0]);
  const cyc = (S.poll.cyclic || []).find(c => c.nr === k.nr);
  const wrap = h('div', { class: 'ctl' });
  if (main === 1) {
    if (dpt === '1.7') {
      wrap.append(h('button', { onclick: () => sendKo(k.nr, dpt, 1) }, 'höher'), h('button', { onclick: () => sendKo(k.nr, dpt, 0) }, 'niedriger'));
    } else {
      const cur = S.kovals[k.nr]?.val;
      wrap.append(h('div', { class: 'seg' },
        h('button', { class: cur === false ? 'active' : '', onclick: () => sendKo(k.nr, dpt, 0) }, '0'),
        h('button', { class: cur === true ? 'active' : '', onclick: () => sendKo(k.nr, dpt, 1) }, '1')));
    }
    return wrap;
  }
  if (main === 20) {
    const cur = S.kovals[k.nr]?.val;
    wrap.append(h('select', { onchange: e => { if (e.target.value !== '') sendKo(k.nr, dpt, e.target.value); } },
      h('option', { value: '' }, '— senden —'), ...HVAC.map(([v, t]) => h('option', { value: v, selected: cur === v }, `${v} ${t}`))));
    return wrap;
  }
  if (main === 16) {
    const inp = h('input', { placeholder: 'Text', size: 10 });
    wrap.append(inp, h('button', { onclick: () => sendKo(k.nr, dpt, inp.value) }, 'senden'));
    return wrap;
  }
  if (dpt === '5.100' || /Stage(Set)?$|GroupStage$/.test(k.name)) {
    const cur = S.kovals[k.nr]?.val;
    wrap.append(h('div', { class: 'seg' }, ...[0, 1, 2, 3, 4].map(s =>
      h('button', { class: cur === s ? 'active' : '', onclick: () => sendKo(k.nr, dpt, s) }, s))));
    return wrap;
  }
  const inp = h('input', { type: 'number', step: main === 9 ? 0.1 : 1, value: cyc ? cyc.value : '',
    placeholder: 'Wert', onkeydown: e => { if (e.key === 'Enter') send(false); } });
  const send = cyclic => { if (inp.value !== '') sendKo(k.nr, dpt, inp.value.replace(',', '.'), cyclic ? cycPeriod() : 0); };
  wrap.append(inp, h('button', { onclick: () => send(!!cyc), title: 'Senden (Eingabetaste)' }, 'senden'));
  if (main === 9 || main === 7 || main === 5)
    wrap.append(h('button', { class: 'icon' + (cyc ? ' active' : ''), title: cyc ? `sendet alle ${cyc.period} s – klicken zum Beenden` : `zyklisch senden, alle ${cycPeriod()} s (wie ein Sensor)`,
      onclick: () => cyc ? act({ action: 'cyclic_off', nr: k.nr }) : send(true) }, '⟳'));
  return wrap;
}

function renderInputs(force) {
  const vis = [...S.visKos.entries()];
  const drv = envDriven();
  const sig = vis.map(x => x.join(':')).join(',') + '|' + JSON.stringify(S.poll.cyclic) + '|' + [...drv].join(',') +
    '|' + JSON.stringify(S.poll.active) + '|' + S.inRoom;
  if (!force && sig === S.sigInputs) { updateKoValues(); return; }
  if (!force && document.activeElement && $('#tab-inputs').contains(document.activeElement) && document.activeElement.tagName === 'INPUT') { updateKoValues(); return; }
  S.sigInputs = sig;
  S.closed = S.closed || new Set();
  const groups = new Map();
  for (const [nr, dpt] of vis) {
    const k = S.kos[nr];
    // Bedienung: nur Raum- und Lüfterobjekte. Die OpenKNX-Grundobjekte
    // (In Betrieb, Uhrzeit, Diagnose) bildet der Simulator nicht nach.
    if (!k || !/^(ROOM|FAN)_/.test(k.name)) continue;
    const ch = channelOfKo(k);
    if (!groups.has(ch)) groups.set(ch, { inp: new Map(), out: [] });
    const g = groups.get(ch);
    if (k.inp) {
      const sec = sectionOf(k);
      if (!g.inp.has(sec)) g.inp.set(sec, []);
      g.inp.get(sec).push([k, dpt || k.dpts[0] || '']);
    } else g.out.push([k, dpt || k.dpts[0] || '']);
  }
  const section = (ch, title, body, count) => {
    const key = ch + '|' + title;
    const d = h('details', { open: !S.closed.has(key) },
      h('summary', {}, title, count ? h('span', { class: 'muted' }, ` (${count})`) : null), body);
    d.addEventListener('toggle', () => d.open ? S.closed.delete(key) : S.closed.add(key));
    return d;
  };
  const cardOf = new Map();
  for (const ch of [...groups.keys()].sort((a, b) => chRank(a) - chRank(b))) cardOf.set(ch, (() => {
    const g = groups.get(ch);
    const row = ([k, dpt]) => h('div', { class: 'korow', title: `${k.f}\nObjekt ${k.nr} · DPT ${dpt}` },
      h('span', { class: 'nr' }, k.nr),
      h('span', { class: 'lbl' }, koLabel(k),
        drv.has(k.nr) ? h('span', { class: 'tag env', title: 'wird von der Umgebungssimulation gesendet' }, 'Umgebung') : null),
      inputControl(k, dpt),
      h('span', { class: 'val', 'data-ko': k.nr, 'data-dpt': dpt }, koValText(k.nr, dpt)));
    const secs = SECTION_ORDER.filter(s => g.inp.has(s)).map(s =>
      section(ch, s, h('div', {}, ...g.inp.get(s).map(row)), g.inp.get(s).length));
    const outs = g.out.length ? section(ch, 'Ausgänge', h('div', { class: 'outgrid' }, ...g.out.map(([k, dpt]) =>
      h('div', { class: 'outitem', title: `${k.f}\nObjekt ${k.nr} · DPT ${dpt}` },
        h('span', { class: 'lbl' }, h('span', { class: 'nr' }, k.nr), ' ', koLabel(k)),
        h('b', { 'data-ko': k.nr, 'data-dpt': dpt }, koValText(k.nr, dpt))))), g.out.length) : null;
    return h('div', { class: 'chcard' },
      h('h4', {}, chName(ch), h('span', { class: 'st', 'data-chstate': ch }, '')),
      ch.startsWith('Lüfter') ? fanContext(Number(ch.split(' ')[1])) : null, ...secs, outs);
  })());

  // --- Unterreiter je Raum: links der Raum, rechts seine Lüfter ------------
  const rooms = [...cardOf.keys()].filter(c => c.startsWith('Raum'));
  const fansOf = no => [...cardOf.keys()].filter(c => c.startsWith('Lüfter') &&
    Number(S.activeValues[`FAN_f${c.split(' ')[1]}Room`]) === no);
  const orphans = [...cardOf.keys()].filter(c => c.startsWith('Lüfter') &&
    !rooms.includes('Raum ' + Number(S.activeValues[`FAN_f${c.split(' ')[1]}Room`])));
  const tabs = rooms.map(r => ({ key: r, label: chName(r), fans: fansOf(Number(r.split(' ')[1])) }));
  if (orphans.length) tabs.push({ key: 'orphans', label: 'Lüfter ohne aktiven Raum', fans: orphans });
  if (!tabs.some(t => t.key === S.inRoom)) S.inRoom = tabs[0]?.key;
  const cur = tabs.find(t => t.key === S.inRoom);
  const tabbar = h('div', { class: 'subtabs' }, ...tabs.map(t =>
    h('button', { class: t.key === S.inRoom ? 'active' : '', onclick: () => { S.inRoom = t.key; saveInRoom(); renderInputs(true); } },
      t.label, h('span', { class: 'fanchips' }, ...t.fans.map(f => h('span', { class: 'chip' }, 'L' + f.split(' ')[1]))),
      t.key !== 'orphans' ? h('span', { class: 'st', 'data-tabstate': t.key }, '') : null)));
  const pair = cur ? h('div', { class: 'pair' },
    h('div', { class: 'paircol' }, ...(cur.key === 'orphans' ? [h('div', { class: 'emptynote' }, 'Diese Lüfter gehören zu einem Raum, der nicht aktiv ist. Sie laufen nicht.')]
      : [panelWidget(Number(cur.key.split(' ')[1])), cardOf.get(cur.key)].filter(Boolean))),
    h('div', { class: 'paircol' }, ...(cur.fans.length ? cur.fans.map(f => cardOf.get(f))
      : [h('div', { class: 'emptynote' }, 'Diesem Raum ist kein aktiver Lüfter zugeordnet (Parameter Lüfter › Raum).')]))) : null;

  $('#tab-inputs').replaceChildren(
    h('div', { class: 'tools' },
      h('label', { title: 'Gilt für das ⟳ an jedem Messwert' }, 'Zyklisch senden alle ',
        h('input', { type: 'number', min: 1, value: cycPeriod(), style: 'width:5em',
          onchange: e => { try { localStorage.setItem('kwlsim.cyc', e.target.value); } catch (x) {} } }), ' s'),
      h('span', { class: 'muted' }, 'Abschnitte lassen sich zuklappen. „Umgebung“ = Wert kommt aus der Umgebungssimulation und wird beim nächsten Senden überschrieben.')),
    tabs.length ? tabbar : null,
    pair || h('div', { class: 'emptynote' }, 'Keine Objekte sichtbar. Im Reiter „Parameter“ unter Lüftung › Kanalauswahl Räume und Lüfter aktivieren und programmieren.'));
  updateKoValues();
}

// ------------------------------------------------------------ Bedienfeld
// Nachbildung eines KNX-Wandpanels mit Lüftungsseite (Stufe niedrig/mittel/
// hoch, Automatik, Ein/Aus, Sperrobjekt), so verknüpft wie in der
// Applikationsbeschreibung unter "Bedienfelder anbinden":
//   Stufe -> Stufe manuell, Status <- Raumanforderung Stufe,
//   Automatik <-> Handbetrieb/Automatik aktiv, Ein/Aus -> Zwangsobjekt 1.
// Die Polaritäten der Raumseite werden beachtet, das Panel sendet also genau
// das, was ein echtes Bedienfeld an dieser Stelle senden würde.
const PANEL_STAGES = [['niedrig', 1], ['mittel', 2], ['hoch', 3]];

function roomKo(no, suffix) {
  for (const [nr, dpt] of S.visKos.entries()) {
    const k = S.kos[nr];
    if (k && k.name === `ROOM_${no}${suffix}`) return { nr, dpt: dpt || k.dpts[0] || '' };
  }
  return null;
}

function panelState(no) {
  const v = S.activeValues;
  const manualInv = Number(v[`ROOM_r${no}ManualPolarity`]) === 1;
  const forceOnZero = Number(v[`ROOM_r${no}ForcePolarity`]) === 1;
  const ko = { stage: roomKo(no, 'StageSet'), status: roomKo(no, 'DemandStage'),
               auto: roomKo(no, 'ManualAct'), power: roomKo(no, 'Force1') };
  const val = k => k ? S.kovals[k.nr]?.val : undefined;
  const manual = val(ko.auto);                      // Rohwert des Objekts
  const auto = manual === undefined ? true : (manualInv ? manual === true : manual === false);
  const force = val(ko.power);
  const on = force === undefined ? true : (forceOnZero ? force === true : force === false);
  const r = S.poll.state?.sim?.rooms.find(x => x.no === no);
  return { ko, manualInv, forceOnZero, stage: val(ko.status), auto, on, room: r,
           locked: !!(r && (r.locked || r.protection)) };
}

/// Kleine Symbole als SVG - Schriftzeichen wie ⏻ fehlen in vielen Schriften.
function svgIcon(kind) {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('class', 'ico ' + kind);
  const path = d => { const p = document.createElementNS(ns, 'path'); p.setAttribute('d', d); svg.append(p); };
  if (kind === 'power') {
    path('M12 3v9');
    path('M6.3 6.3a8 8 0 1 0 11.4 0');
  } else {
    // Lüfterrad: drei Flügel um eine Nabe
    path('M12 12c0-4 1-7 4-7s3 4-4 7z');
    path('M12 12c3.5 2 5.5 4.5 4 7s-5 0-4-7z');
    path('M12 12c-3.5 2-7 2-8-1s3-4 8 1z');
  }
  return svg;
}

function panelWidget(no) {
  const s = panelState(no);
  if (!s.ko.stage || !s.ko.status) return null;
  const btn = (cls, label, title, onclick, data) =>
    h('button', Object.assign({ class: 'pbtn ' + cls, title, onclick }, data || {}), label);
  const send = (k, value) => { if (k && !panelState(no).locked) sendKo(k.nr, k.dpt, value); };
  return h('div', { class: 'kpanel', 'data-panel': no },
    h('div', { class: 'phead' },
      h('span', { class: 'ptitle' }, 'Bedienfeld · ', chName('Raum ' + no)),
      h('span', { class: 'plock', 'data-p': 'lock', title: 'Sperrobjekt: Fenstersperre oder Schutzbetrieb' }, '🔒 gesperrt')),
    h('div', { class: 'pgrid' },
      s.ko.power ? btn('power', svgIcon('power'), 'Ein/Aus → Zwangsobjekt 1' + (s.forceOnZero ? ' (wirkt bei 0)' : ' (wirkt bei 1)'),
        () => { const on = panelState(no).on; send(s.ko.power, s.forceOnZero ? (on ? 0 : 1) : (on ? 1 : 0)); },
        { 'data-p': 'power' }) : null,
      ...PANEL_STAGES.map(([lbl, st]) => btn('stage', h('span', { class: 'pst' }, h('span', { class: 'fan' }, ...Array.from({ length: st }, () => svgIcon('fan'))), lbl),
        `Stufe ${st} → Stufe manuell`, () => send(s.ko.stage, st), { 'data-p': 'stage', 'data-st': st })),
      s.ko.auto ? btn('auto', 'A', 'Automatik → ' + (s.manualInv ? 'Automatik aktiv (1 = Automatik)' : 'Handbetrieb aktiv (0 = Automatik)'),
        () => { const p = panelState(no); send(s.ko.auto, s.manualInv ? (p.auto ? 0 : 1) : (p.auto ? 1 : 0)); },
        { 'data-p': 'auto' }) : null),
    h('div', { class: 'pfoot', 'data-p': 'foot' }, ''),
    (!s.manualInv || !s.forceOnZero) ? h('div', { class: 'phint' },
      'Hinweis: Für ein Bedienfeld wie dieses gehören auf der Raumseite „Objekt Handbetrieb = 1 = Automatik“ und „Zwangsobjekte wirken bei 0“ (mit Zwangsobjekt 1 = Ruhe). Das Panel sendet hier mit der aktuellen Einstellung.') : null);
}

function updatePanels() {
  for (const el of $$('[data-panel]')) {
    const no = Number(el.dataset.panel), s = panelState(no);
    el.classList.toggle('locked', s.locked);
    el.classList.toggle('off', !s.on);
    $('[data-p="power"]', el)?.classList.toggle('on', s.on);
    $('[data-p="auto"]', el)?.classList.toggle('on', s.auto);
    for (const b of $$('[data-p="stage"]', el)) {
      const st = Number(b.dataset.st);
      b.classList.toggle('on', s.on && (s.stage === st || (st === 3 && s.stage === 4)));
    }
    const r = s.room;
    const foot = $('[data-p="foot"]', el);
    if (foot) foot.textContent = r ? `Status Stufe ${s.stage ?? '—'} · ${r.mode} · ${r.source} · ${r.cycle}` +
      (r.locked ? ' · Fenster/Sperre' : '') + (r.protection ? ' · Schutz' : '') : '';
  }
}

function saveInRoom() { try { localStorage.setItem('kwlsim.inRoom', S.inRoom); } catch (e) {} }

/// Zuordnung eines Lüfters auf einen Blick: Kanal, Takt, Phase, Partner im Verbund.
function fanContext(no) {
  const v = S.activeValues, grp = Number(v[`FAN_f${no}Group`]);
  const partners = [];
  for (let n = 1; n <= 12; n++)
    if (n !== no && Number(v[`FAN_f${n}Active`]) === 1 && grp && Number(v[`FAN_f${n}Group`]) === grp)
      partners.push(`Lüfter ${n} (${chName('Raum ' + Number(v[`FAN_f${n}Room`]))}, Phase ${Number(v[`FAN_f${n}Phase`]) ? 'B' : 'A'})`);
  const type = (S.params[`FAN_f${no}Type`]?.enum || []).find(e => e[0] === Number(v[`FAN_f${no}Type`]))?.[1] || '';
  return h('div', { class: 'fanctx' },
    h('span', {}, type),
    h('span', {}, Number(v[`FAN_f${no}Type`]) === 1   // ego: zwei Motoren, zwei Kanäle
      ? `Stellkanal S${v[`FAN_f${no}Channel`]}/S${Number(v[`FAN_f${no}Channel`]) + 1}` : `Stellkanal S${v[`FAN_f${no}Channel`]}`),
    h('span', {}, grp ? `Verbund ${grp} · Phase ${Number(v[`FAN_f${no}Phase`]) ? 'B' : 'A'}` : 'kein Verbund – Fehlercode 3'),
    Number(v[`FAN_f${no}Share`]) < 100 ? h('span', {}, `Anteil ${v[`FAN_f${no}Share`]} %`) : null,
    grp ? h('span', { class: 'muted' }, partners.length ? 'Partner: ' + partners.join(', ') : 'kein Partner im Verbund') : null);
}

function updateKoValues() {
  for (const el of $$('[data-ko]')) {
    const txt = koValText(Number(el.dataset.ko), el.dataset.dpt);
    if (el.textContent !== txt) {
      el.textContent = txt;
      const rowEl = el.closest('tr, .korow, .outitem');
      rowEl?.classList.remove('flash'); void el.offsetWidth; rowEl?.classList.add('flash');
    }
  }
  const st = S.poll.state?.sim;
  if (!st) return;
  updatePanels();
  for (const el of $$('[data-tabstate]')) {
    const r = st.rooms.find(x => 'Raum ' + x.no === el.dataset.tabstate);
    el.textContent = r ? `Stufe ${r.stage}` : '';
  }
  for (const el of $$('[data-chstate]')) {
    const [kind, no] = el.dataset.chstate.split(' ');
    let txt = '';
    if (kind === 'Raum') { const r = st.rooms.find(x => x.no == no); if (r) txt = `Stufe ${r.stage} · ${r.source} · ${r.mode}`; }
    if (kind === 'Lüfter') { const f = st.fans.find(x => x.no == no); if (f) txt = `S${f.ch} · Stufe ${f.stage}${f.stage ? ' ' + f.dir : ''}${f.err ? ' · Fehler ' + f.err : ''}`; }
    el.textContent = txt;
  }
}

// ------------------------------------------------------------ Objekte
function renderKos(force) {
  if (!$('#kos-tools')) {
    $('#tab-kos').replaceChildren(
      h('div', { class: 'tools', id: 'kos-tools' },
        h('input', { id: 'kos-filter', placeholder: 'Filter: Nummer oder Name', oninput: () => renderKos(true) }),
        h('select', { id: 'kos-ch', onchange: () => renderKos(true) }, h('option', { value: '' }, 'alle Kanäle')),
        h('select', { id: 'kos-dir', onchange: () => renderKos(true) },
          h('option', { value: '' }, 'Ein- und Ausgänge'), h('option', { value: 'in' }, 'nur Eingänge'), h('option', { value: 'out' }, 'nur Ausgänge'))),
      h('p', { class: 'hint' }, 'Alle Objekte, die die ETS mit der programmierten Parametrierung einblendet. S = schreiben (Eingang), Ü = übertragen (Ausgang). Wert = zuletzt gesendet oder empfangen.'),
      h('div', { class: 'scroll' }, h('table', { class: 'grid', id: 'kos-table' })));
  }
  const f = $('#kos-filter').value.trim().toLowerCase(), chSel = $('#kos-ch').value, dir = $('#kos-dir').value;
  const sig = [...S.visKos.keys()].join(',') + '|' + f + '|' + chSel + '|' + dir;
  if (!force && sig === S.sigKos) { updateKoValues(); return; }
  S.sigKos = sig;
  const chans = [...new Set([...S.visKos.keys()].map(nr => S.kos[nr] && channelOfKo(S.kos[nr])).filter(Boolean))].sort((a, b) => chRank(a) - chRank(b));
  const sel = $('#kos-ch');
  if (sel.dataset.sig !== chans.join()) {
    sel.dataset.sig = chans.join();
    sel.replaceChildren(h('option', { value: '' }, 'alle Kanäle'), ...chans.map(c => h('option', { value: c, selected: c === chSel }, c)));
  }
  const rows = [];
  let lastCh = null;
  const list = [...S.visKos.entries()].filter(([nr]) => S.kos[nr])
    .sort((a, b) => chRank(channelOfKo(S.kos[a[0]])) - chRank(channelOfKo(S.kos[b[0]])) || a[0] - b[0]);
  for (const [nr, dpt] of list) {
    const k = S.kos[nr], ch = channelOfKo(k);
    if (chSel && ch !== chSel) continue;
    if (dir === 'in' && !k.inp) continue;
    if (dir === 'out' && !k.out) continue;
    if (f && !(String(nr) === f || k.t.toLowerCase().includes(f) || k.name.toLowerCase().includes(f))) continue;
    if (ch !== lastCh) { rows.push(h('tr', { class: 'grouprow' }, h('td', { colspan: 7 }, chName(ch)))); lastCh = ch; }
    const v = S.kovals[nr];
    rows.push(h('tr', {},
      h('td', { class: 'num' }, nr), h('td', {}, koLabel(k), h('div', { class: 'muted' }, k.f.replace(/^(Raum|Lüfter) \d+:\s*/, ''))),
      h('td', { class: 'mono' }, dpt), h('td', {}, k.size),
      h('td', {}, (k.inp ? 'S' : '') + (k.out ? 'Ü' : '')),
      h('td', { class: 'num', 'data-ko': nr, 'data-dpt': dpt }, koValText(nr, dpt)),
      h('td', { class: 'mono muted' }, v ? `${v.dir === 'rx' ? '→' : '←'} ${fmtT(v.t)}` : '')));
  }
  $('#kos-table').replaceChildren(h('tr', {}, ...['Nr', 'Objekt', 'DPT', 'Größe', 'Flags', 'Wert', 'zuletzt'].map(x => h('th', {}, x))), ...rows);
}

// ------------------------------------------------------------ Parameter
function pageVisible(pg) { return condOk(pg.cond, S.values); }

function renderPageList() {
  const vis = S.pages.filter(pageVisible);
  if (!S.page || !vis.includes(S.page)) S.page = vis.find(p => p.title === 'Kanalauswahl') || vis[0];
  const depth = {};
  const items = [];
  let lastTop = '';
  for (const pg of vis) {
    const d = pg.parent && depth[pg.parent] !== undefined ? depth[pg.parent] + 1 : (pg.parent === 'OpenKNX' || pg.parent === 'Lüftung' ? 0 : 1);
    depth[pg.title] = d;
    const top = pg.parent === 'OpenKNX' ? 'OpenKNX' : 'Lüftung';
    if (top !== lastTop) { items.push(h('div', { class: 'ch' }, top)); lastTop = top; }
    items.push(h('button', { class: `d${Math.min(d, 3)}` + (pg === S.page ? ' active' : ''),
      onclick: () => { S.page = pg; renderParams(true); } }, pageTitle(pg)));
  }
  $('#pagelist').replaceChildren(...items);
}

function pageTitle(pg) {
  const name = pg.textParam ? S.values[pg.textParam] : '';
  if (name && String(name).trim()) return pg.title.replace('...', name);
  return pg.title.replace(/:\s*\.\.\.$/, '').replace('...', '…');
}

function paramControl(p, name) {
  const cur = S.values[name];
  const set = v => act({ action: 'param', name, value: v });
  if (p.k === 'enum') {
    return h('select', { onchange: e => set(e.target.value) },
      ...p.enum.map(([v, t]) => h('option', { value: v, selected: Number(cur) === v }, t)));
  }
  if (p.k === 'bool') return h('input', { type: 'checkbox', checked: Number(cur) === 1, onchange: e => set(e.target.checked ? 1 : 0) });
  if (p.k === 'text') return h('input', { value: cur ?? '', onchange: e => set(e.target.value) });
  return h('span', {}, h('input', { type: 'number', value: cur, min: p.min, max: p.max, step: p.k === 'float' ? 'any' : 1,
    onchange: e => set(e.target.value) }), p.unit ? h('span', { class: 'unit' }, ' ' + p.unit) : null,
    p.min !== undefined ? h('span', { class: 'unit', title: 'Wertebereich' }, ` (${p.min}…${p.max})`) : null);
}

function renderParams(force) {
  const sig = JSON.stringify(S.poll.pending) + '|' + JSON.stringify(S.poll.active) + '|' + (S.page && S.page.title + S.page.parent);
  if (!force && sig === S.sigParams) return;
  if (!force && document.activeElement && $('#page').contains(document.activeElement)) return;
  S.sigParams = sig;
  renderPageList();
  const pg = S.page; if (!pg) return;
  const pend = diffValues();
  const out = [h('h3', {}, pageTitle(pg))];
  const seen = new Set();
  let row = null, rowKey = null;
  const flushRow = () => { if (row) out.push(row); row = null; rowKey = null; };
  for (const it of pg.items) {
    if (it.type === 'k' || it.type === 'a') continue;
    if (!condOk(it.cond, S.values)) continue;
    let el = null;
    if (it.type === 'p') {
      const p = S.params[it.n];
      if (!p || p.hidden || seen.has(it.n)) continue;
      seen.add(it.n);
      const changed = S.values[it.n] !== p.d;
      if (it.row) {
        el = h('span', { class: 'pv' + (it.n in pend ? ' pending' : ''), title: `${p.t}\n${it.n}` }, paramControl(p, it.n));
      } else {
        el = h('div', { class: `pitem ind${Math.min(it.indent || 0, 2)}` + (changed ? ' changed' : '') + (it.n in pend ? ' pending' : ''), title: it.n },
          h('span', { class: 'pl' }, p.t), h('span', { class: 'pv' }, paramControl(p, it.n)));
      }
    } else if (it.type === 'h' && it.row) { el = h('span', { class: 'colhead' }, it.text); }
    else if (it.type === 'h') { flushRow(); out.push(h('div', { class: 'phead' }, it.text)); continue; }
    else if (it.type === 'i') { flushRow(); out.push(h('div', { class: 'pinfo' }, it.text)); continue; }
    else if (it.type === 'e') { flushRow(); out.push(h('div', { class: 'perr' }, it.text)); continue; }
    else if (it.type === 'r') { flushRow(); continue; }
    else if (it.type === 'l') { el = it.text ? h('span', { class: 'lab' }, it.text) : null; }
    if (!el) continue;
    if (it.row) {
      if (rowKey !== it.row) { flushRow(); row = h('div', { class: 'prow' }); rowKey = it.row; }
      if (it.type === 'h') row.classList.add('head');
      row.append(el);
    } else { flushRow(); out.push(el); }
  }
  flushRow();
  const n = Object.keys(pend).length;
  out.unshift(h('div', { class: 'tools' },
    h('button', { class: 'primary', onclick: () => act({ action: 'program' }) }, n ? `Programmieren (${n} Änderungen)` : 'Programmieren'),
    h('button', { onclick: () => act({ action: 'params_revert' }) }, 'Änderungen verwerfen'),
    h('button', { onclick: () => { if (confirm('Alle Parameter auf Vorgabe setzen?')) act({ action: 'params_defaults' }); } }, 'Alle auf Vorgabe'),
    h('span', { class: 'muted' }, '● = weicht von der Vorgabe ab · gelber Rahmen = noch nicht programmiert')));
  $('#page').replaceChildren(...out);
}

// ------------------------------------------------------------ Telegramme, Konsole, Pruefung
function renderBus() {
  const f = $('#bus-filter').value.trim().toLowerCase();
  const rx = $('#bus-rx').checked, tx = $('#bus-tx').checked;
  const rows = [];
  for (let i = S.busRows.length - 1; i >= 0 && rows.length < 400; i--) {
    const e = S.busRows[i];
    if ((e.ev === 'rx' && !rx) || (e.ev === 'ko' && !tx)) continue;
    const k = S.kos[e.nr] || { t: '?', name: '' };
    if (f && !(String(e.nr) === f || k.t.toLowerCase().includes(f) || k.name.toLowerCase().includes(f))) continue;
    rows.push(h('tr', { class: e.ev === 'rx' ? 'rx' : 'tx' }, h('td', { class: 'mono' }, fmtT(e.t)),
      h('td', {}, e.ev === 'rx' ? (e.delivered ? '→ empfangen' : '→ verworfen') : '← gesendet'),
      h('td', { class: 'num' }, e.nr), h('td', {}, k.t), h('td', { class: 'mono' }, e.dpt),
      h('td', { class: 'mono' }, fmtKo(e.nr, e.val, e.dpt))));
  }
  $('#bus-table').replaceChildren(h('tr', {}, ...['Zeit', 'Richtung', 'Nr', 'Objekt', 'DPT', 'Wert'].map(x => h('th', {}, x))), ...rows);
}

function renderLog() {
  if (!S.logDirty) return;
  S.logDirty = false;
  const el = $('#log');
  const atEnd = el.scrollTop + el.clientHeight >= el.scrollHeight - 30;
  el.replaceChildren(...S.logLines.slice(-1500).map(l => h('div', { class: l.cls }, l.text)));
  if (atEnd) el.scrollTop = el.scrollHeight;
}

function renderChecks() {
  $('#checks-table').replaceChildren(h('tr', {}, h('th', {}, 'Zeit'), h('th', {}, 'Art'), h('th', {}, 'Meldung')),
    ...S.checks.slice(-600).reverse().map(c => h('tr', {}, h('td', { class: 'mono' }, fmtT(c.t || 0)),
      h('td', { class: c.cls }, c.kind), h('td', {}, c.text))));
}

// ------------------------------------------------------------ Gesamt
let lastRender = 0;
function render() {
  if (!S.poll) return;
  renderBar();
  renderOutputs();
  renderChart();
  renderTod();
  if (S.tab === 'inputs') renderInputs();
  else if (S.tab === 'env') renderEnv();
  else if (S.tab === 'params') renderParams();
  else if (S.tab === 'kos') renderKos();
  else if (S.tab === 'bus') renderBus();
  else if (S.tab === 'console') renderLog();
  else if (S.tab === 'checks') renderChecks();
}

function setupUi() {
  $$('#tabs button').forEach(b => b.addEventListener('click', () => {
    S.tab = b.dataset.tab;
    $$('#tabs button').forEach(x => x.classList.toggle('active', x === b));
    $$('.tabbody').forEach(x => x.classList.toggle('hidden', x.id !== 'tab-' + S.tab));
    if (S.tab === 'checks') S.warnCount = 0;
    S.logDirty = true; S.sigInputs = S.sigParams = S.sigKos = S.sigEnv = '';
    try { history.replaceState(null, '', '#' + S.tab); } catch (e) {}
    render();
  }));
  // Reiter aus der Adresse (#env, #params ...): bleibt beim Neuladen erhalten.
  const fromHash = location.hash.slice(1);
  $$('#tabs button').find(b => b.dataset.tab === fromHash)?.click();
  $$('#speed [data-speed]').forEach(b => b.addEventListener('click', () => act({ action: 'speed', value: b.dataset.speed })));
  $$('#speed [data-step]').forEach(b => b.addEventListener('click', () => act({ action: 'step', ms: b.dataset.step })));
  $('#pwr-bus').addEventListener('click', () => act({ action: 'power', which: 'bus', on: !S.poll.state.bus }));
  $('#pwr-v12').addEventListener('click', () => act({ action: 'power', which: 'v12', on: !S.poll.state.v12 }));
  $('#btn-program').addEventListener('click', () => act({ action: 'program' }));
  $('#btn-restart').addEventListener('click', () => act({ action: 'restart' }));
  $('#btn-unload').addEventListener('click', () => { if (confirm('Applikation entladen? Das Gerät startet ohne Parameter, wie neu.')) act({ action: 'unload' }); });
  $$('[data-btn]').forEach(b => b.addEventListener('click', () => act({ action: 'btn', kind: b.dataset.btn })));
  $('#project').addEventListener('change', e => { if (e.target.value) act({ action: 'project_load', name: e.target.value }).then(() => { S.dac = []; S.dacIdx = 0; S.checks = []; S.busRows = []; }); });
  $('#btn-save').addEventListener('click', () => {
    const name = prompt('Projektname:', S.poll.project || 'Mein Aufbau');
    if (name) act({ action: 'project_save', name });
  });
  $$('#window button').forEach(b => b.addEventListener('click', () => {
    S.win = Number(b.dataset.win); $$('#window button').forEach(x => x.classList.toggle('active', x === b)); renderChart();
  }));
  const sendCmd = text => act({ action: 'console', text, diag: $('#cmd-diag').checked });
  $('#cmd-send').addEventListener('click', () => { const t = $('#cmd').value.trim(); if (t) sendCmd(t); });
  $('#cmd').addEventListener('keydown', e => { if (e.key === 'Enter') { const t = e.target.value.trim(); if (t) sendCmd(t); } });
  $$('[data-cmd]').forEach(b => b.addEventListener('click', () => sendCmd(b.dataset.cmd)));
  $('#log-clear').addEventListener('click', () => { S.logLines = []; S.logDirty = true; renderLog(); });
  $('#bus-clear').addEventListener('click', () => { S.busRows = []; renderBus(); });
  for (const id of ['#bus-filter', '#bus-rx', '#bus-tx']) $(id).addEventListener('input', renderBus);
  window.addEventListener('resize', () => { renderChart(); if (S.tab === 'env') renderEnvChart(); });
  $$('#env-q button').forEach(b => b.addEventListener('click', () => {
    S.envQ = b.dataset.q; $$('#env-q button').forEach(x => x.classList.toggle('active', x === b)); renderEnvChart();
  }));
  $$('#env-win button').forEach(b => b.addEventListener('click', () => {
    S.envWin = Number(b.dataset.win); $$('#env-win button').forEach(x => x.classList.toggle('active', x === b)); renderEnvChart();
  }));
}

// ------------------------------------------------------------ Umgebung
const ENV_Q = {
  temp: 'Temperatur innen', hum: 'Feuchte innen', co2: 'CO₂', voc: 'VOC',
  out: 'Außenwerte', set: 'Solltemperatur',
};
const ENV_KO = { temp: ['TempIn'], hum: ['HumIn'], co2: ['CO2'], voc: ['VOC'], out: ['TempOut', 'HumOut'], set: ['TempSet'] };
const WEEKDAY = ['Mo', 'Di', 'Mi', 'Do', 'Fr', 'Sa', 'So'];
const envAct = patch => act({ action: 'env', patch });
const fmtTod = (h, day) => `${WEEKDAY[day % 7]} ${String(Math.floor(h)).padStart(2, '0')}:${String(Math.floor((h % 1) * 60)).padStart(2, '0')}`;

function renderTod() {
  const e = S.poll.env, el = $('#tod');
  el.classList.toggle('hidden', !e?.config.enabled);
  if (e?.config.enabled) el.textContent = fmtTod(e.tod, e.day);
}

function renderEnv() {
  const e = S.poll.env; if (!e) return;
  const c = e.config;
  const sig = JSON.stringify([c, e.rooms, e.targets]);
  if (sig !== S.sigEnv && !(document.activeElement && $('#tab-env').contains(document.activeElement) && document.activeElement.tagName === 'INPUT' && document.activeElement.type === 'number')) {
    S.sigEnv = sig;
    const num = (key, attrs, unit) => h('input', Object.assign({ type: 'number', value: c[key], onchange: ev => envAct({ [key]: Number(ev.target.value) }) }, attrs));
    $('#env-global').replaceChildren(
      h('div', { class: 'envbar' },
        h('label', { title: 'Die Umgebung erzeugt Messwerte und sendet sie wie echte Sensoren' },
          h('input', { type: 'checkbox', checked: c.enabled, onchange: ev => envAct({ enabled: ev.target.checked }) }), h('b', {}, 'Umgebungssimulation aktiv')),
        h('label', {}, 'Jahreszeit', h('select', { onchange: ev => envAct({ season: ev.target.value }) },
          ...e.seasons.map(s => h('option', { value: s, selected: s === c.season }, s)))),
        h('label', { title: 'Tageszeit beim Start der Simulationszeit (Projekt laden setzt sie zurück)' }, 'Start um', num('startHour', { min: 0, max: 23.5, step: 0.5 }), 'Uhr'),
        h('label', { title: 'Stärke der Zufallsschwankungen: Wetter, Messrauschen' }, 'Zufall',
          h('input', { type: 'range', min: 0, max: 3, step: 0.1, value: c.strength, onchange: ev => envAct({ strength: Number(ev.target.value) }) }),
          h('span', { class: 'mono' }, de(c.strength, 1))),
        h('label', { title: 'Der Luftstrom, den die Firmware meldet, wirkt auf den Raum zurück' },
          h('input', { type: 'checkbox', checked: c.coupling, onchange: ev => envAct({ coupling: ev.target.checked }) }), 'Kopplung an Lüftung'),
        h('label', { title: 'Duschen im Bad, Kochen in der Küche' },
          h('input', { type: 'checkbox', checked: c.events, onchange: ev => envAct({ events: ev.target.checked }) }), 'Ereignisse'),
        h('label', { title: 'Sensoren senden bei Änderung und spätestens nach dieser Zeit' }, 'Sendeintervall', num('cycleMin', { min: 0.5, max: 60, step: 0.5 }), 'min'),
        h('label', { title: 'Startwert des Zufallsgenerators: gleicher Wert, gleicher Ablauf' }, 'Startwert', num('seed', { min: 1, step: 1 }),
          h('button', { onclick: () => envAct({ seed: Math.floor(Math.random() * 100000) + 1 }) }, 'Neu würfeln'))),
      h('div', { class: 'envout', id: 'env-out' }));

    const cards = e.rooms.map(no => {
      const rc = c.rooms[String(no)] || {};
      const tg = e.targets[no] || {};
      const rnum = (key, attrs, unit) => h('label', {}, attrs.label, h('input', { type: 'number', value: rc[key], min: attrs.min, max: attrs.max, step: attrs.step,
        onchange: ev => envAct({ room: no, [key]: Number(ev.target.value) }) }), unit);
      const send = Object.entries(ENV_Q).map(([q, label]) => {
        const kos = ENV_KO[q].map(k => tg[k]).filter(Boolean);
        return h('label', { title: kos.length ? 'Objekt ' + kos.join(', ') : 'Objekt in der ETS nicht eingeblendet - wird nicht gesendet' },
          h('input', { type: 'checkbox', checked: !!(rc.send || {})[q], disabled: !kos.length,
            onchange: ev => envAct({ room: no, [q]: ev.target.checked }) }),
          label, h('span', { class: 'ko' }, kos.length ? ' ' + kos.join('/') : ' —'));
      });
      return h('div', { class: 'chcard' },
        h('h4', {}, chName(`Raum ${no}`), h('span', { class: 'st', 'data-envstate': no }, '')),
        h('div', { class: 'envform' },
          h('label', {}, 'Profil', h('select', { onchange: ev => envAct({ room: no, profile: ev.target.value }) },
            ...e.profiles.map(p => h('option', { value: p, selected: p === rc.profile }, p)))),
          rnum('volume', { label: 'Volumen', min: 5, max: 500, step: 1 }, 'm³'),
          rnum('persons', { label: 'Personen', min: 0, max: 10, step: 1 }, ''),
          rnum('setpoint', { label: 'Soll', min: 10, max: 30, step: 0.5 }, '°C')),
        h('div', { class: 'envvals', 'data-envvals': no }),
        h('div', { class: 'envsend' }, h('span', { class: 'muted' }, 'Sendet:'), ...send));
    });
    $('#env-rooms').replaceChildren(...(cards.length ? cards : [h('div', { class: 'envoff' }, 'Kein Raum aktiv.')]));
  }
  // Werte in place aktualisieren
  const o = e.out;
  $('#env-out').replaceChildren(...(o ? [
    h('span', {}, 'Außen ', h('b', {}, de(o.T, 1) + ' °C')), h('span', {}, h('b', {}, de(o.RH, 0) + ' %'), ' rF'),
    h('span', {}, h('b', {}, de(o.x, 1)), ' g/kg'),
    h('span', { class: 'muted' }, c.enabled ? `${fmtTod(e.tod, e.day)} · Tag ${e.day + 1}` : 'Umgebungssimulation aus — keine Sensortelegramme')]
    : [h('span', { class: 'muted' }, 'Noch keine Werte. Umgebung aktivieren und die Zeit laufen lassen.')]));
  for (const el of $$('[data-envvals]')) {
    const s = e.status[el.dataset.envvals];
    if (!s) { el.replaceChildren(h('span', { class: 'muted' }, '—')); continue; }
    const cell = (label, v) => h('div', {}, h('small', {}, label), h('b', {}, v));
    el.replaceChildren(
      cell('Temperatur', de(s.T, 1) + ' °C'), cell('rel. Feuchte', de(s.RH, 0) + ' %'), cell('abs. Feuchte', de(s.x, 1) + ' g/kg'),
      cell('CO₂', de(s.CO2, 0) + ' ppm'), cell('VOC', de(s.VOC, 0) + ' ppb'), cell('Personen', s.persons),
      cell('Luftstrom', de(s.Q, 0) + ' m³/h'), cell('Luftwechsel', de(s.n, 2) + ' 1/h'), cell('WRG', s.hrv ? 'ja' : '—'));
  }
  for (const el of $$('[data-envstate]')) {
    const s = e.status[el.dataset.envstate];
    el.replaceChildren(s?.event ? h('span', { class: 'event' }, s.event) : document.createTextNode(s ? s.profile : ''));
  }
  renderOccupant(e);
  renderEnvChart();
}

/// Bewohner: Einstellungen und die letzten Aktionen.
function renderOccupant(e) {
  const o = e.occupant; if (!o) return;
  const c = o.config;
  const occAct = patch => act({ action: 'occupant', patch });
  const sig = JSON.stringify(c);
  const box = $('#env-occ');
  if (box.dataset.sig !== sig) {
    box.dataset.sig = sig;
    box.replaceChildren(
      h('h3', {}, 'Bewohner'),
      h('p', { class: 'hint' }, 'Ein simulierter Mensch bedient die Anlage über dieselben Objekte wie ein Taster: Tagesablauf, Handstufe bei stickiger Luft, Fenster auf (Sperre), Badlüftung beim Duschen, Funktionstaste. Er braucht die Umgebungssimulation, um zu sehen, wer da ist und ob geduscht wird.'),
      h('div', { class: 'envbar' },
        h('label', {}, h('input', { type: 'checkbox', checked: c.enabled, onchange: ev => occAct({ enabled: ev.target.checked }) }), h('b', {}, 'Bewohner aktiv')),
        h('label', { title: 'Wie oft er eingreift: 0 = nie, 1 = normal, 3 = ständig' }, 'Aktivität',
          h('input', { type: 'range', min: 0, max: 3, step: 0.1, value: c.activity, onchange: ev => occAct({ activity: Number(ev.target.value) }) }),
          h('span', { class: 'mono' }, de(c.activity, 1))),
        h('label', {}, 'Startwert', h('input', { type: 'number', value: c.seed, min: 1, style: 'width:5em', onchange: ev => occAct({ seed: Number(ev.target.value) }) }))),
      h('div', { class: 'envsend' }, ...Object.entries(o.behaviours).map(([k, label]) =>
        h('label', {}, h('input', { type: 'checkbox', checked: !!c.behave[k], onchange: ev => occAct({ behave: { [k]: ev.target.checked } }) }), label))),
      h('div', { class: 'occplan', id: 'occ-plan' }),
      h('div', { class: 'scroll occlog' }, h('table', { class: 'grid', id: 'occ-log' })));
  }
  const p = o.plan || {};
  const hm = x => x === null || x === undefined ? '—' : `${String(Math.floor(x)).padStart(2, '0')}:${String(Math.floor((x % 1) * 60)).padStart(2, '0')}`;
  $('#occ-plan').textContent = c.enabled && p.wake !== undefined
    ? `Heute: aufstehen ${hm(p.wake)} · ${p.leave === null ? 'zuhause' : 'aus dem Haus ' + hm(p.leave) + ' bis ' + hm(p.back)} · schlafen ${hm(p.bed)}`
    : (c.enabled ? 'Tagesplan entsteht beim nächsten Schritt.' : 'Bewohner aus.');
  const rows = (o.log || []).slice().reverse().map(([t, room, text]) => h('tr', {},
    h('td', { class: 'mono' }, fmtTod((e.config.startHour + t / 3600000) % 24, Math.floor((e.config.startHour + t / 3600000) / 24))),
    h('td', {}, room ? chName('Raum ' + room) : 'Gerät'), h('td', {}, text)));
  $('#occ-log').replaceChildren(h('tr', {}, h('th', {}, 'Wann'), h('th', {}, 'Wo'), h('th', {}, 'Aktion')),
    ...(rows.length ? rows : [h('tr', {}, h('td', { colspan: 3, class: 'muted' }, 'noch keine Aktion'))]));
}

function renderEnvChart() {
  const c = $('#envchart'); if (!c || !S.poll?.env) return;
  const dpr = window.devicePixelRatio || 1;
  const w = c.clientWidth, hgt = c.clientHeight || 260;
  if (!w) return;
  if (c.width !== Math.round(w * dpr) || c.height !== Math.round(hgt * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(hgt * dpr); }
  const g = c.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  g.clearRect(0, 0, w, hgt);
  const L = 40, R = 8, T = 8, B = 20, pw = w - L - R, ph = hgt - T - B;
  const now = S.poll.state?.t || 0, t0 = Math.max(0, now - S.envWin);
  const rows = S.envHist.filter(r => r[0] >= t0);
  const idx = { T: 0, RH: 1, CO2: 2, VOC: 3, Q: 5 }[S.envQ];
  const series = [];
  const rooms = S.poll.env.rooms;
  if (S.envQ === 'T' || S.envQ === 'RH')
    series.push({ name: 'außen', color: css('--muted'), dash: [4, 3], pts: rows.map(r => [r[0], S.envQ === 'T' ? r[1] : r[2]]) });
  const palette = [css('--s1'), css('--s2'), css('--s3'), css('--s4'), '#e11d48', '#0891b2', '#65a30d', '#a16207'];
  rooms.forEach((no, i) => series.push({ name: 'R' + no, color: palette[i % 8], pts: rows.filter(r => r[3][no]).map(r => [r[0], r[3][no][idx]]) }));
  let lo = Infinity, hi = -Infinity;
  for (const s of series) for (const p of s.pts) { lo = Math.min(lo, p[1]); hi = Math.max(hi, p[1]); }
  if (!isFinite(lo)) { g.fillStyle = css('--muted'); g.font = '12px system-ui'; g.fillText('Noch kein Verlauf - Umgebung aktivieren und Zeit laufen lassen.', L, T + 20); return; }
  if (S.envQ === 'CO2') lo = Math.min(lo, 400);
  if (S.envQ === 'Q') lo = 0;
  const pad = (hi - lo) * 0.08 || 1; lo -= pad; hi += pad;
  const X = t => L + (t - t0) / Math.max(1, now - t0) * pw;
  const Y = v => T + (1 - (v - lo) / (hi - lo)) * ph;
  g.font = '11px system-ui'; g.lineWidth = 1;
  for (let k = 0; k <= 4; k++) {
    const v = lo + (hi - lo) * k / 4;
    g.strokeStyle = css('--line'); g.beginPath(); g.moveTo(L, Y(v)); g.lineTo(L + pw, Y(v)); g.stroke();
    g.fillStyle = css('--muted'); g.fillText(de(v, S.envQ === 'T' ? 1 : 0), 2, Y(v) + 4);
  }
  // Tageszeit-Raster: volle Stunden der Umgebung
  const startH = S.poll.env.config.startHour;
  const stepH = S.envWin > 86400000 ? 12 : S.envWin > 21600000 ? 3 : 1;
  for (let t = t0 - (t0 % 3600000); t <= now; t += 3600000) {
    const hr = Math.round((startH + t / 3600000) % 24);
    if (t < t0 || hr % stepH) continue;
    g.fillStyle = css('--muted'); g.fillText(String(hr).padStart(2, '0') + ':00', X(t) - 14, hgt - 5);
    g.strokeStyle = css('--line'); g.setLineDash([2, 4]); g.beginPath(); g.moveTo(X(t), T); g.lineTo(X(t), T + ph); g.stroke(); g.setLineDash([]);
  }
  for (const s of series) {
    g.strokeStyle = s.color; g.lineWidth = 1.6; g.setLineDash(s.dash || []); g.beginPath();
    s.pts.forEach((p, i) => i ? g.lineTo(X(p[0]), Y(p[1])) : g.moveTo(X(p[0]), Y(p[1])));
    g.stroke(); g.setLineDash([]);
  }
  let lx = L + 6;
  for (const s of series) {
    g.fillStyle = s.color; g.fillRect(lx, T + 2, 10, 3);
    g.fillStyle = css('--text'); g.fillText(s.name, lx + 13, T + 7); lx += 46;
  }
}

async function main() {
  S.meta = await (await fetch('/api/meta')).json();
  S.params = S.meta.params;
  S.pages = S.meta.pages;
  for (const k of S.meta.kos) S.kos[k.nr] = k;
  setupUi();
  await poll();
  setInterval(poll, 250);
}
main();
