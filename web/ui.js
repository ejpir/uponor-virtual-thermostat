// Loaded from <head>, so wait for the body before building the page.
document.addEventListener('DOMContentLoaded', () => {
  const CONFIRM_TIMEOUT_MS = 30000;
  const SEND_DELAY_MS = 900;
  const STEP = 0.5;
  const MAX_ROOMS = 8;

  const css = `
:root{color-scheme:light dark;--bg:#f4f3ef;--card:#fff;--ink:#1d1f21;--muted:#6b6f73;--line:#e6e4dd;
--btn:#f0eee8;--heat:#c8501e;--ok:#2e7d4f;--warn:#a8650a;--bad:#b3261e;--room:#1f6fb2;--set:#c8501e;--grid:#ecebe6}
@media (prefers-color-scheme:dark){:root{--bg:#141517;--card:#1f2124;--ink:#f1f1ee;--muted:#a0a4a8;
--line:#303337;--btn:#2c2f33;--heat:#f08a4b;--ok:#6cc792;--warn:#e6ae57;--bad:#f2857c;--room:#4f95d0;--set:#dc7536;--grid:#2b2e31}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
font-variant-numeric:tabular-nums}
main{max-width:460px;margin:0 auto;padding:18px 16px 40px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}
h1{font-size:20px;font-weight:650;margin:0}
.pill{display:inline-flex;align-items:center;gap:7px;font-size:13px;color:var(--muted);white-space:nowrap}
.dot{width:9px;height:9px;border-radius:50%;background:var(--muted)}
.pill.ok .dot{background:var(--ok)}.pill.warn .dot{background:var(--warn)}.pill.bad .dot{background:var(--bad)}
.card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:20px;margin-bottom:14px}
.notice{border-color:var(--warn);color:var(--warn);font-size:15px;padding:14px 16px}
.notice.bad{border-color:var(--bad);color:var(--bad)}
.hero{text-align:center;padding:24px 20px}
.label{font-size:14px;color:var(--muted)}
.now{font-size:72px;line-height:1.05;font-weight:600;letter-spacing:-.02em;margin:6px 0 2px}
.now small{font-size:30px;font-weight:500;color:var(--muted);letter-spacing:0}
.action{font-size:15px;font-weight:600;color:var(--muted);min-height:21px}
.action.heating{color:var(--heat)}
.set{display:grid;grid-template-columns:64px 1fr 64px;align-items:center;gap:10px;margin-top:22px;
padding-top:20px;border-top:1px solid var(--line)}
button{font:inherit;color:inherit;border:0;cursor:pointer}
.step{height:64px;border-radius:50%;background:var(--btn);font-size:30px;font-weight:500;line-height:1}
.step:active:not(:disabled){transform:scale(.94)}
button:disabled{opacity:.4;cursor:default}
button:focus-visible{outline:2px solid var(--heat);outline-offset:2px}
.target{font-size:34px;font-weight:600;line-height:1.15}
.hint{font-size:13px;color:var(--muted);min-height:20px;margin-top:10px}
.hint.bad{color:var(--bad)}
h2{font-size:14px;font-weight:600;color:var(--muted);margin:22px 4px 8px}
.rooms{padding:4px 18px}
.room{display:flex;align-items:baseline;justify-content:space-between;gap:12px;padding:13px 0;border-top:1px solid var(--line)}
.room:first-child{border-top:0}
.room b{font-size:19px;font-weight:600}
.room span{font-size:13px;color:var(--muted);margin-left:8px}

.chart{padding:16px 16px 12px}
.chart h2{margin:0 0 10px;color:var(--ink);font-size:15px}
.legend{display:flex;flex-wrap:wrap;gap:4px 14px;font-size:13px;color:var(--muted)}
.key{display:inline-flex;align-items:center;gap:6px}
.key i{width:16px;height:2px;border-radius:1px;background:var(--room)}
.key i.target{background:var(--set)}
.key i.band{height:11px;border-radius:3px;background:var(--heat);opacity:.16}
#plot{position:relative;margin-top:8px;touch-action:pan-y}
#plot svg{display:block;width:100%;overflow:visible}
#plot text{font-size:11px;fill:var(--muted)}
.read{font-size:13px;color:var(--muted);min-height:20px;margin-top:6px}
.read b{color:var(--ink);font-weight:600}
.empty{font-size:14px;color:var(--muted);padding:26px 0;text-align:center}
.chart details{margin-top:6px;font-size:13px}
.chart summary{padding:6px 0}
table{width:100%;border-collapse:collapse}
th,td{text-align:right;padding:4px 0;font-weight:400}
th:first-child,td:first-child{text-align:left}
th{color:var(--muted)}td{color:var(--ink)}
details{margin-top:16px;color:var(--muted);font-size:14px}
summary{cursor:pointer;padding:8px 4px}
dl{margin:6px 4px 14px;display:grid;grid-template-columns:auto 1fr;gap:6px 14px}
dt{color:var(--muted)}dd{margin:0;color:var(--ink);overflow-wrap:anywhere}
.quiet{background:var(--btn);border-radius:12px;padding:11px 16px;margin:0 4px 6px;font-size:14px}
`;

  const el = (tag, attrs = {}, ...kids) => {
    const n = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) n.setAttribute(k, v);
    n.append(...kids);
    return n;
  };

  document.title = 'Heating';
  document.head.append(
    el('meta', { name: 'viewport', content: 'width=device-width,initial-scale=1,viewport-fit=cover' }),
    el('meta', { name: 'theme-color', content: '#f4f3ef', media: '(prefers-color-scheme:light)' }),
    el('meta', { name: 'theme-color', content: '#141517', media: '(prefers-color-scheme:dark)' }),
  );
  const style = el('style');
  style.textContent = css;
  document.head.append(style);

  document.body.innerHTML = `<main>
<header><h1>Heating</h1><div id=link class=pill role=status><i class=dot></i><span>Connecting</span></div></header>
<div id=notice class="card notice" role=status hidden></div>
<section class="card hero" aria-label="Main thermostat">
<div class=label>Main thermostat</div>
<div class=now><span id=now>--</span><small>°C</small></div>
<div id=action class=action></div>
<div class=set>
<button id=down class=step aria-label="Lower target temperature" disabled>−</button>
<div><div class=label>Target</div><div id=target class=target aria-live=polite>--</div></div>
<button id=up class=step aria-label="Raise target temperature" disabled>+</button>
</div>
<div id=hint class=hint role=status></div>
</section>
<section class="card chart" aria-label="Temperature history">
<h2>Last 24 hours</h2>
<div class=legend><span class=key><i></i>Room</span><span class=key><i class=target></i>Target</span><span class=key><i class=band></i>Heating</span></div>
<div id=plot></div><div id=read class=read></div>
<details id=tableWrap hidden><summary>Show as table</summary><table><thead><tr><th>Time</th><th>Room</th><th>Target</th></tr></thead><tbody id=table></tbody></table></details>
</section>
<div id=roomsWrap hidden><h2>Other rooms</h2><section id=rooms class="card rooms"></section></div>
<details><summary>Device details</summary>
<dl><dt>Bus</dt><dd id=bus>--</dd><dt>Main</dt><dd id=main>--</dd><dt>Found</dt><dd id=found>--</dd></dl>
<button id=rescan class=quiet>Re-run detection</button>
</details></main>`;

  const $ = id => document.getElementById(id);
  const fmt = v => (Number.isFinite(v) ? v.toFixed(1) : '--');

  const s = {
    current: NaN, target: NaN, min: 5, max: 35, action: '', bus: '', main: '', found: [],
    temp: Array(MAX_ROOMS).fill(NaN), setpoint: Array(MAX_ROOMS).fill(NaN),
  };
  let desired = null;
  let sendTimer = 0;
  let confirmTimer = 0;
  let failed = false;

  const ready = () => s.bus === 'Ready' && Number.isFinite(s.target);

  function renderLink(state) {
    const link = $('link');
    link.className = 'pill ' + state;
    link.lastChild.textContent = state === 'bad' ? 'Offline' : 'Live';
  }

  function render() {
    $('now').textContent = fmt(s.current);
    const heating = s.action === 'HEATING' || s.action === 'COOLING';
    $('action').textContent = s.action === 'HEATING' ? 'Heating' : s.action === 'COOLING' ? 'Cooling' : s.action ? 'Not heating' : '';
    $('action').className = 'action' + (heating ? ' heating' : '');

    const shown = desired ?? s.target;
    $('target').textContent = Number.isFinite(shown) ? fmt(shown) + '°' : '--';
    $('down').disabled = !ready() || shown <= s.min;
    $('up').disabled = !ready() || shown >= s.max;

    const hint = $('hint');
    hint.className = 'hint' + (failed ? ' bad' : '');
    hint.textContent = failed ? 'The thermostat did not confirm that. Showing its real setting.'
      : desired !== null ? 'Sending to the thermostat…' : '';

    const notice = $('notice');
    const wiring = s.bus.startsWith('No bus');
    notice.hidden = !s.bus || s.bus === 'Ready';
    notice.className = 'card notice' + (wiring ? ' bad' : '');
    notice.textContent = wiring ? 'No data from the heating bus. Check the wiring to the stick.'
      : s.bus === 'Detecting thermostats' ? 'Finding your thermostats. This takes about 2 minutes.'
      : 'Could not work out which thermostat is the main one, so this is read-only for now.';

    const mainIndex = s.found.indexOf(s.main);
    const rooms = $('rooms');
    rooms.replaceChildren();
    s.temp.forEach((t, i) => {
      if (i === mainIndex || !Number.isFinite(t)) return;
      const value = el('div', {}, el('b', {}, fmt(t) + '°'));
      if (Number.isFinite(s.setpoint[i])) value.append(el('span', {}, 'set ' + fmt(s.setpoint[i]) + '°'));
      rooms.append(el('div', { class: 'room' }, el('div', {}, 'Room ' + (i + 1)), value));
    });
    $('roomsWrap').hidden = !rooms.children.length;

    $('bus').textContent = s.bus || '--';
    $('main').textContent = s.main || '--';
    $('found').textContent = s.found.join(', ') || '--';
  }

  function settle(isFailure) {
    clearTimeout(confirmTimer);
    desired = null;
    failed = isFailure;
    render();
  }

  function send() {
    const value = desired;
    fetch('/climate/Main%20Thermostat/set?target_temperature=' + value, { method: 'POST' })
      .then(r => { if (!r.ok) throw new Error(r.status); })
      .catch(() => settle(true));
    clearTimeout(confirmTimer);
    confirmTimer = setTimeout(() => settle(true), CONFIRM_TIMEOUT_MS);
  }

  function nudge(direction) {
    const base = desired ?? s.target;
    desired = Math.min(s.max, Math.max(s.min, Math.round((base + direction * STEP) / STEP) * STEP));
    failed = false;
    clearTimeout(sendTimer);
    clearTimeout(confirmTimer);
    render();
    if (desired === s.target) {
      desired = null;
      render();
      return;
    }
    sendTimer = setTimeout(send, SEND_DELAY_MS);
  }

  $('down').onclick = () => nudge(-1);
  $('up').onclick = () => nudge(1);
  $('rescan').onclick = () => {
    if (confirm('Search for thermostats again? Control is paused for about 2 minutes.'))
      fetch('/button/Re-run%20Detection/press', { method: 'POST' });
  };

  function onState(e) {
    const d = JSON.parse(e.data);
    const room = /^sensor-room_(\d)_(temperature|setpoint)$/.exec(d.id);
    if (room) {
      const value = typeof d.value === 'number' ? d.value : NaN;
      s[room[2] === 'temperature' ? 'temp' : 'setpoint'][room[1] - 1] = value;
    } else if (d.id === 'climate-main_thermostat') {
      s.current = parseFloat(d.current_temperature);
      s.target = parseFloat(d.target_temperature);
      s.min = parseFloat(d.min_temp) || s.min;
      s.max = parseFloat(d.max_temp) || s.max;
      s.action = d.action || '';
      if (desired !== null && s.target === desired) return settle(false);
    } else if (d.id === 'text_sensor-bus_status') {
      s.bus = d.value;
    } else if (d.id === 'text_sensor-main_thermostat_address') {
      s.main = d.value === 'None' ? '' : d.value;
    } else if (d.id === 'text_sensor-detected_thermostats') {
      s.found = d.value === 'None yet' ? [] : d.value.split(', ');
    }
    render();
  }

  const SVG_NS = 'http://www.w3.org/2000/svg';
  const svg = (tag, attrs) => {
    const n = document.createElementNS(SVG_NS, tag);
    for (const [k, v] of Object.entries(attrs)) n.setAttribute(k, v);
    return n;
  };
  const clock = ms => new Date(ms).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  let history = null;

  function renderChart() {
    const plot = $('plot');
    plot.replaceChildren();
    $('read').textContent = '';
    const samples = history ? history.samples : [];
    if (samples.length < 2) {
      plot.append(el('div', { class: 'empty' }, 'Collecting data. The graph starts after about 5 minutes.'));
      $('tableWrap').hidden = true;
      return;
    }

    const end = Date.now() - history.age * 1000;
    const points = samples.map((p, i) => ({
      t: end - (samples.length - 1 - i) * history.interval * 1000, room: p[0] / 10, target: p[1] / 10, heating: p[2],
    }));

    const W = plot.clientWidth, H = 180, L = 30, R = 6, T = 8, B = 22;
    const t0 = points[0].t, t1 = Math.max(end, t0 + 3600000);
    const all = points.flatMap(p => [p.room, p.target]);
    let lo = Math.floor(Math.min(...all)), hi = Math.ceil(Math.max(...all));
    if (hi - lo < 4) { lo -= Math.floor((4 - hi + lo) / 2); hi = lo + 4; }
    const x = t => L + (t - t0) / (t1 - t0) * (W - L - R);
    const y = v => T + (hi - v) / (hi - lo) * (H - T - B);

    const chart = svg('svg', { viewBox: `0 0 ${W} ${H}`, height: H, role: 'img' });
    const last = points[points.length - 1];
    chart.setAttribute('aria-label', `Room temperature from ${fmt(Math.min(...points.map(p => p.room)))} to ` +
      `${fmt(Math.max(...points.map(p => p.room)))} degrees since ${clock(t0)}, now ${fmt(last.room)}, target ${fmt(last.target)}.`);

    const yStep = Math.max(1, Math.ceil((hi - lo) / 4));
    for (let v = lo; v <= hi; v += yStep) {
      chart.append(svg('line', { x1: L, x2: W - R, y1: y(v), y2: y(v), stroke: 'var(--grid)', 'stroke-width': 1 }));
      const label = svg('text', { x: L - 6, y: y(v) + 4, 'text-anchor': 'end' });
      label.textContent = v + '°';
      chart.append(label);
    }

    const span = t1 - t0;
    const xStep = (span > 43200000 ? 6 : span > 14400000 ? 3 : 1) * 3600000;
    const offset = new Date().getTimezoneOffset() * 60000;
    for (let t = Math.ceil((t0 - offset) / xStep) * xStep + offset; t <= t1; t += xStep) {
      const label = svg('text', { x: x(t), y: H - 6, 'text-anchor': 'middle' });
      label.textContent = clock(t);
      if (x(t) > L + 14 && x(t) < W - R - 14) chart.append(label);
    }

    for (let i = 0; i < points.length - 1; i++) {
      if (!points[i].heating) continue;
      const start = i;
      while (i < points.length - 1 && points[i].heating) i++;
      chart.append(svg('rect', {
        x: x(points[start].t), y: T, width: x(points[i].t) - x(points[start].t), height: H - T - B,
        fill: 'var(--heat)', opacity: .16,
      }));
    }

    const line = (d, color) => chart.append(svg('path', {
      d, fill: 'none', stroke: color, 'stroke-width': 2, 'stroke-linejoin': 'round', 'stroke-linecap': 'round',
    }));
    line(points.map((p, i) => (i ? `H${x(p.t)}V${y(p.target)}` : `M${x(p.t)} ${y(p.target)}`)).join(''), 'var(--set)');
    line(points.map((p, i) => `${i ? 'L' : 'M'}${x(p.t)} ${y(p.room)}`).join(''), 'var(--room)');

    const cross = svg('line', { y1: T, y2: H - B, stroke: 'var(--muted)', 'stroke-width': 1, visibility: 'hidden' });
    const dot = svg('circle', { r: 4, fill: 'var(--room)', stroke: 'var(--card)', 'stroke-width': 2, visibility: 'hidden' });
    chart.append(cross, dot);
    plot.append(chart);

    const inspect = e => {
      const px = e.clientX - chart.getBoundingClientRect().left;
      const p = points.reduce((a, b) => (Math.abs(x(b.t) - px) < Math.abs(x(a.t) - px) ? b : a));
      for (const [k, v] of [['x1', x(p.t)], ['x2', x(p.t)], ['visibility', 'visible']]) cross.setAttribute(k, v);
      for (const [k, v] of [['cx', x(p.t)], ['cy', y(p.room)], ['visibility', 'visible']]) dot.setAttribute(k, v);
      $('read').replaceChildren(el('b', {}, fmt(p.room) + '°'), ' room · ', el('b', {}, fmt(p.target) + '°'),
        ` target · ${p.heating ? 'heating' : 'not heating'} · ${clock(p.t)}`);
    };
    chart.onpointerdown = inspect;
    chart.onpointermove = inspect;

    const rows = points.filter((_, i) => (points.length - 1 - i) % 12 === 0).reverse();
    $('table').replaceChildren(...rows.map(p => el('tr', {}, el('td', {}, clock(p.t)), el('td', {}, fmt(p.room) + '°'),
      el('td', {}, fmt(p.target) + '°'))));
    $('tableWrap').hidden = false;
  }

  function loadHistory() {
    fetch('/history').then(r => r.json()).then(h => { history = h; renderChart(); }).catch(() => {});
  }
  addEventListener('resize', renderChart);
  setInterval(loadHistory, 60000);
  loadHistory();

  const source = new EventSource('/events');
  source.addEventListener('state', onState);
  source.addEventListener('ping', () => renderLink('ok'));
  source.onopen = () => renderLink('ok');
  source.onerror = () => renderLink('bad');
  render();
});
