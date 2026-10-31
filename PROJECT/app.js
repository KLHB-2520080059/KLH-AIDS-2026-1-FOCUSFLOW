'use strict';
const $ = (s, r = document) => r.querySelector(s);
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const fmtDate = t => new Date(t * 1000).toLocaleString([], { dateStyle: 'medium', timeStyle: 'short' });
const S = { user: null, settings: null, procs: null, last: null, lastMeta: null };

async function api(path, data) {
  const opt = { headers: { 'X-Requested-With': 'fetch' } };
  if (data) { opt.method = 'POST'; opt.headers['Content-Type'] = 'application/x-www-form-urlencoded'; opt.body = new URLSearchParams(data); }
  try {
    const r = await fetch('/api/' + path, opt);
    const j = await r.json();
    if (r.status === 401 && S.user && path !== 'password' && path !== 'account/delete') { S.user = null; location.hash = '#/login'; }
    return j;
  } catch (e) { return { ok: false, error: 'Cannot reach the server. Is it still running?' }; }
}
function toast(msg, bad) {
  const t = document.createElement('div'); t.className = 'toast' + (bad ? ' bad' : ''); t.textContent = msg;
  $('#toasts').appendChild(t); setTimeout(() => t.remove(), 3500);
}
function setTheme(t) { document.documentElement.dataset.theme = t; localStorage.setItem('theme', t); }
const hue = i => `hsl(${(i * 57 + 215) % 360} 62% 48%)`;
const ALGO = ['Priority (preemptive)', 'Priority (non-preemptive)'];

/* ---------- auth ---------- */
function authView(mode) {
  const su = mode === 'signup';
  $('#app').innerHTML = `<div class="auth"><section class="hero">
    <div class="brand"><span class="logo">⚙</span> PrioSched</div>
    <h1>Priority scheduling that doesn’t starve anyone.</h1>
    <p class="muted">Operating System &amp; System Programming (25CS2104E) — a Linux priority scheduler written in C, with an aging mechanism, real <code>fork()</code>/<code>exec()</code> processes and live metrics.</p>
    <ul><li>Simulate preemptive &amp; non-preemptive priority scheduling</li><li>Compare results with and without aging side by side</li>
    <li>Run the schedule on real Linux processes (SIGSTOP / SIGCONT, pipes)</li><li>Save, review and compare your runs</li></ul></section>
    <section class="authbox"><div class="card"><div class="tabs"><a href="#/login" class="${su ? '' : 'on'}">Log in</a><a href="#/signup" class="${su ? 'on' : ''}">Sign up</a></div>
    <h2>${su ? 'Create your account' : 'Welcome back'}</h2>
    <form id="af">${su ? '<label>Full name</label><input name="name" required maxlength="60" autocomplete="name">' : ''}
    <label>Email</label><input name="email" type="email" required autocomplete="email">
    <label>Password ${su ? '<span class="muted">(min 8 characters)</span>' : ''}</label><input name="password" type="password" required minlength="${su ? 8 : 1}" autocomplete="${su ? 'new-password' : 'current-password'}">
    ${su ? '<label>Confirm password</label><input name="confirm" type="password" required autocomplete="new-password">' : ''}
    <div class="err" id="aerr"></div><button class="btn block">${su ? 'Create account' : 'Log in'}</button></form></div></section></div>`;
  $('#af').onsubmit = async e => {
    e.preventDefault(); const f = Object.fromEntries(new FormData(e.target));
    if (su && f.password !== f.confirm) { $('#aerr').textContent = 'Passwords do not match.'; return; }
    delete f.confirm; const btn = $('#af button'); btn.disabled = true;
    const r = await api(su ? 'signup' : 'login', f); btn.disabled = false;
    if (!r.ok) { $('#aerr').textContent = r.error; return; }
    S.user = r.user; S.settings = r.settings; setTheme(S.settings.theme); S.procs = null; S.last = null; location.hash = '#/dashboard';
  };
}

/* ---------- shell ---------- */
const NAV = [['dashboard', '🏠', 'Dashboard'], ['scheduler', '📊', 'Scheduler'], ['history', '🕘', 'History'], ['profile', '👤', 'Profile'], ['settings', '⚙️', 'Settings'], ['about', 'ℹ️', 'About']];
function shell(active, html) {
  if (!$('.shell')) {
    $('#app').innerHTML = `<div class="shell"><aside class="side"><div class="brand"><span class="logo">⚙</span> PrioSched</div>
      ${NAV.map(n => `<a class="nav" data-r="${n[0]}" href="#/${n[0]}"><span>${n[1]}</span>${n[2]}</a>`).join('')}
      <div class="foot"><div class="row"><div class="avatar" id="sav"></div><div style="min-width:0"><b id="sname" style="display:block;overflow:hidden;text-overflow:ellipsis"></b><a href="#" id="lo" class="small">Log out</a></div></div></div></aside><main id="view"></main></div>`;
    $('#lo').onclick = async e => { e.preventDefault(); await api('logout', {}); S.user = null; location.hash = '#/login'; };
  }
  document.querySelectorAll('.side a.nav').forEach(a => a.classList.toggle('on', a.dataset.r === active));
  $('#sav').textContent = S.user.name.trim().slice(0, 1).toUpperCase(); $('#sname').textContent = S.user.name;
  const old = $('#view'), fresh = old.cloneNode(false); old.replaceWith(fresh); /* fresh node = no stale listeners */
  fresh.innerHTML = html; return fresh;
}

/* ---------- dashboard ---------- */
async function dashboard() {
  const v = shell('dashboard', `<h1>Hello, ${esc(S.user.name.split(' ')[0])} 👋</h1><p class="muted">Your scheduling lab at a glance.</p><div id="dst" class="grid g4"></div>
    <div class="grid g2"><div class="card"><h2>Recent runs</h2><div id="drec" class="muted">Loading…</div></div>
    <div class="card"><h2>Quick start</h2><p class="muted">Jump straight into an experiment:</p><div class="row">
    <a class="btn" href="#/scheduler" id="q1">New simulation</a><a class="btn ghost" href="#/scheduler" id="q2">Starvation demo</a></div>
    <h3 style="margin-top:1.4rem">How it works</h3><p class="muted small">The scheduler always runs the ready process with the smallest priority number. With <b>aging</b>, a process that waits long enough gets its number reduced step by step, so low-priority work is eventually served — the answer to starvation.</p></div></div>`);
  $('#q2').onclick = () => { S.preset = 'starve'; };
  const [st, rn] = await Promise.all([api('stats'), api('runs')]); if (!$('#dst')) return;
  const red = st.mw_noaging > 0 ? Math.round((1 - st.mw_aging / st.mw_noaging) * 100) : 0;
  $('#dst').innerHTML = [[st.runs, 'Saved runs'], [st.procs, 'Processes scheduled'], [st.avg_wait.toFixed(1), 'Avg waiting time'], [red + '%', 'Worst-wait cut by aging']]
    .map(s => `<div class="stat"><b>${s[0]}</b><span>${s[1]}</span></div>`).join('');
  $('#drec').innerHTML = rn.runs.length ? `<table><tbody>${rn.runs.slice(0, 5).map(r => `<tr><td><a href="#/history">${esc(r.title)}</a></td><td class="muted small">${fmtDate(r.created)}</td><td class="num">${r.avg_wait.toFixed(1)}</td></tr>`).join('')}</tbody></table>` : 'No saved runs yet — head to the Scheduler and tick “Save to history”.';
}

/* ---------- scheduler ---------- */
const PRESETS = {
  basic: [['P1', 0, 8, 3], ['P2', 1, 4, 1], ['P3', 2, 9, 4], ['P4', 3, 5, 2], ['P5', 4, 2, 5]],
  starve: [['Low', 0, 5, 8], ['H1', 1, 3, 1], ['H2', 3, 3, 1], ['H3', 5, 3, 1], ['H4', 7, 3, 1], ['H5', 9, 3, 1], ['H6', 11, 3, 1]],
};
const toProcs = a => a.map(p => ({ name: p[0], arrival: p[1], burst: p[2], prio: p[3] }));
function procRows() {
  return S.procs.map((p, i) => `<tr>${[['name', 'text', 72], ['arrival', 'number', 0], ['burst', 'number', 0], ['prio', 'number', 0]].map(k =>
    `<td><input data-i="${i}" data-k="${k[0]}" type="${k[1]}" value="${esc(p[k[0]])}" ${k[1] === 'number' ? 'min="0" max="200"' : 'maxlength="20"'}></td>`).join('')}
    <td><button class="btn ghost sm" data-del="${i}" title="Remove">✕</button></td></tr>`).join('');
}
function scheduler() {
  const st = S.settings;
  if (!S.procs) S.procs = toProcs(PRESETS.basic);
  const opt = S.opts || (S.opts = { algo: st.algo, aging: st.aging, interval: st.aging_interval, step: st.aging_step, real: st.real_mode, tick: st.tick_ms });
  if (S.preset === 'starve') { S.procs = toProcs(PRESETS.starve); Object.assign(opt, { aging: 1, interval: 2, step: 2, algo: 0 }); S.preset = null; S.last = null; }
  const v = shell('scheduler', `<div class="row spread"><div><h1>Scheduler</h1><p class="muted">Lower priority number = higher priority (like Linux <code>nice</code>).</p></div></div>
  <div class="split"><div><div class="card"><div class="row spread"><h2>Processes</h2><div class="row">
    <select id="preset" style="width:auto"><option value="">Presets…</option><option value="basic">Basic example</option><option value="starve">Starvation demo</option><option value="rand">Random set</option></select></div></div>
    <div class="tablewrap"><table class="ptable"><thead><tr><th>Name</th><th>Arrive</th><th>Burst</th><th>Prio</th><th></th></tr></thead><tbody id="prow">${procRows()}</tbody></table></div>
    <button class="btn ghost sm" id="addp" style="margin-top:.6rem">+ Add process</button></div>
  <div class="card"><h2>Options</h2><label>Algorithm</label><select id="o_algo"><option value="0">Priority — preemptive</option><option value="1">Priority — non-preemptive</option></select>
    <label class="check"><input type="checkbox" id="o_aging"> Aging enabled for the main result</label>
    <div class="grid g2"><div><label>Aging interval (ticks)</label><input id="o_int" type="number" min="1" max="50"></div><div><label>Aging step</label><input id="o_step" type="number" min="1" max="20"></div></div>
    <label class="check"><input type="checkbox" id="o_real"> Run on real Linux processes</label>
    <div id="realbox"><label>Tick length (ms)</label><input id="o_tick" type="number" min="20" max="500"><p class="muted small">Forks one real child per process and controls it with SIGSTOP/SIGCONT — takes (ticks × tick length) of real time.</p></div>
    <label class="check"><input type="checkbox" id="o_save" checked> Save to history</label><input id="o_title" placeholder="Run title (optional)" maxlength="60">
    <div class="err" id="serr"></div><button class="btn block" id="runbtn" style="margin-top:.4rem">▶ Run schedule</button></div></div>
  <div id="result"><div class="card muted">Run a schedule to see the Gantt chart, metrics and an aging comparison.</div></div></div>`);
  $('#o_algo').value = opt.algo; $('#o_aging').checked = !!opt.aging; $('#o_int').value = opt.interval; $('#o_step').value = opt.step; $('#o_real').checked = !!opt.real; $('#o_tick').value = opt.tick;
  const realToggle = () => { $('#realbox').style.display = $('#o_real').checked ? '' : 'none'; }; realToggle();
  const grab = () => { Object.assign(opt, { algo: +$('#o_algo').value, aging: $('#o_aging').checked ? 1 : 0, interval: +$('#o_int').value, step: +$('#o_step').value, real: $('#o_real').checked ? 1 : 0, tick: +$('#o_tick').value }); };
  v.addEventListener('input', e => { const t = e.target; if (t.dataset.i !== undefined) S.procs[+t.dataset.i][t.dataset.k] = t.type === 'number' ? +t.value : t.value; grab(); realToggle(); });
  v.addEventListener('click', e => { const d = e.target.dataset.del; if (d !== undefined) { S.procs.splice(+d, 1); $('#prow').innerHTML = procRows(); } });
  $('#addp').onclick = () => { if (S.procs.length >= 20) return toast('Maximum 20 processes', 1); S.procs.push({ name: 'P' + (S.procs.length + 1), arrival: 0, burst: 4, prio: 3 }); $('#prow').innerHTML = procRows(); };
  $('#preset').onchange = e => {
    const k = e.target.value; if (!k) return;
    if (k === 'rand') S.procs = Array.from({ length: 6 }, (_, i) => ({ name: 'P' + (i + 1), arrival: Math.floor(Math.random() * 8), burst: 2 + Math.floor(Math.random() * 8), prio: Math.floor(Math.random() * 8) }));
    else { S.procs = toProcs(PRESETS[k]); if (k === 'starve') { Object.assign(opt, { aging: 1, interval: 2, step: 2, algo: 0 }); } }
    S.preset = null; scheduler();
  };
  $('#runbtn').onclick = async () => {
    grab(); const b = $('#runbtn'); b.disabled = true; b.innerHTML = `<span class="spin"></span>${opt.real ? 'Running real processes…' : 'Running…'}`; $('#serr').textContent = '';
    const d = { procs: S.procs.map(p => `${String(p.name).replace(/[,\n]/g, '')},${p.arrival},${p.burst},${p.prio}`).join('\n'), algo: opt.algo, aging: opt.aging, aging_interval: opt.interval, aging_step: opt.step,
      real_mode: opt.real, tick_ms: opt.tick, save: $('#o_save').checked ? 1 : 0, title: $('#o_title').value || 'Run ' + new Date().toLocaleString() };
    const r = await api('simulate', d); b.disabled = false; b.textContent = '▶ Run schedule';
    if (!r.ok) { $('#serr').textContent = r.error; return; }
    S.last = r.result; S.lastMeta = null; renderResult(r.result); if (r.saved_id) toast('Run saved to history');
  };
  if (S.last) renderResult(S.last);
}

function gantt(R) {
  const t0 = R.gantt[0].s, T = R.total, last = R.gantt[R.gantt.length - 1].e;
  const segs = R.gantt.map(g => { const w = (g.e - g.s) / T * 100, p = R.procs[g.p];
    return `<div class="seg" style="width:${w}%"><div class="bar ${g.p < 0 ? 'idle' : ''}" style="${g.p >= 0 ? 'background:' + hue(g.p) : ''}" title="${g.p < 0 ? 'Idle' : esc(p.name)} ${g.s}–${g.e}">${w > 4 ? (g.p < 0 ? 'idle' : esc(p.name)) : ''}</div>${w > 3.2 || g.s === t0 ? `<i>${g.s}</i>` : ''}</div>`; }).join('');
  return `<div class="gantt">${segs.replace(/<\/div>$/, `<i class="end">${last}</i></div>`)}</div>`;
}
function renderResult(res) {
  const o = res.opts, sel = o.aging ? res.aging : res.noaging, A = res.aging, N = res.noaging;
  const delta = (a, b, lowerBetter = true) => { const d = a - b; if (Math.abs(d) < 0.005) return '<span class="muted">—</span>'; const good = lowerBetter ? d < 0 : d > 0; return `<span class="chip ${good ? 'good' : 'bad'}">${d > 0 ? '+' : ''}${d.toFixed(2)}</span>`; };
  const metrics = [['Avg waiting time', 'avg_wait', 1], ['Avg turnaround', 'avg_tat', 1], ['Avg response', 'avg_resp', 1], ['Worst waiting time', 'max_wait', 1], ['CPU utilisation %', 'util', 0], ['Context switches', 'ctx', 1], ['Throughput (proc/tick)', 'throughput', 0]];
  const maxW = Math.max(1, ...A.procs.map(p => p.wait), ...N.procs.map(p => p.wait));
  const legend = sel.procs.map((p, i) => `<span><b style="background:${hue(i)}"></b>${esc(p.name)}</span>`).join('');
  let h = `<div class="card"><div class="row spread"><h2>Results</h2><div class="row"><span class="chip acc">${ALGO[o.algo]}</span><span class="chip ${o.aging ? 'good' : ''}">Aging ${o.aging ? `on (every ${o.interval}, −${o.step})` : 'off'}</span></div></div>
    <div class="grid g6">${[['Avg wait', sel.avg_wait.toFixed(2)], ['Avg turnaround', sel.avg_tat.toFixed(2)], ['Avg response', sel.avg_resp.toFixed(2)], ['CPU util', sel.util.toFixed(1) + '%'], ['Ctx switches', sel.ctx], ['Worst wait', sel.max_wait]]
      .map(s => `<div class="stat"><b>${s[1]}</b><span>${s[0]}</span></div>`).join('')}</div></div>
    <div class="card"><h2>Gantt chart — ${o.aging ? 'with aging' : 'without aging'}</h2><div class="legend">${legend}</div>${gantt(sel)}
    <h3>${o.aging ? 'Without aging' : 'With aging'} (comparison)</h3>${gantt(o.aging ? N : A)}</div>
    <div class="card"><h2>Aging vs. no aging</h2><div class="tablewrap"><table><thead><tr><th>Metric</th><th class="num">Without aging</th><th class="num">With aging</th><th class="num">Change</th></tr></thead><tbody>
    ${metrics.map(m => `<tr><td>${m[0]}</td><td class="num">${N[m[1]]}</td><td class="num">${A[m[1]]}</td><td class="num">${delta(A[m[1]], N[m[1]], m[2])}</td></tr>`).join('')}</tbody></table></div>
    <p class="muted small">Aging trades a little average efficiency for fairness: the longest wait (starvation indicator) usually drops.</p></div>
    <div class="card"><h2>Per-process details${o.aging ? ' (with aging)' : ''}</h2><div class="tablewrap"><table><thead><tr><th>Process</th><th class="num">Arrival</th><th class="num">Burst</th><th class="num">Priority</th><th class="num">First run</th><th class="num">Finish</th><th class="num">Wait</th><th class="num">Turnaround</th><th class="num">Response</th></tr></thead><tbody>
    ${sel.procs.map((p, i) => `<tr><td><b style="color:${hue(i)}">●</b> ${esc(p.name)}</td><td class="num">${p.arrival}</td><td class="num">${p.burst}</td><td class="num">${p.prio}${o.aging && p.fprio !== p.prio ? ` → ${p.fprio}` : ''}</td><td class="num">${p.first}</td><td class="num">${p.finish}</td><td class="num">${p.wait}</td><td class="num">${p.tat}</td><td class="num">${p.resp}</td></tr>`).join('')}</tbody></table></div></div>
    <div class="card bars"><h2>Waiting time per process</h2><p class="small"><span style="color:var(--warn)">■ without aging</span> &nbsp; <span style="color:var(--accent2)">■ with aging</span></p>
    ${A.procs.map((p, i) => `<div class="line"><span>${esc(p.name)}</span><div><div class="track"><div class="fill n" style="width:${N.procs[i].wait / maxW * 100}%"></div></div><div class="track"><div class="fill a" style="width:${p.wait / maxW * 100}%"></div></div></div><span class="num mono">${N.procs[i].wait} → ${p.wait}</span></div>`).join('')}</div>`;
  if (res.real) h += `<div class="card"><h2>Real Linux processes</h2><p class="muted small">Every process ran as a real child (<code>fork</code> + <code>exec</code>), paused/resumed with <code>SIGSTOP</code>/<code>SIGCONT</code>; each child reports progress through a pipe.</p>
    <div class="row"><span class="chip acc">${res.real.elapsed_ms} ms of CPU slices</span><span class="chip good">setpriority ok: ${res.real.nice_ok}</span><span class="chip ${res.real.nice_fail ? 'bad' : ''}">denied: ${res.real.nice_fail}</span></div>
    <table><thead><tr><th>Process</th><th class="num">PID</th><th class="num">Pipe heartbeats</th></tr></thead><tbody>${res.real.procs.map((p, i) => `<tr><td>${esc(sel.procs[i].name)}</td><td class="num mono">${p.pid}</td><td class="num">${p.beats}</td></tr>`).join('')}</tbody></table></div>`;
  else if (o.real) h += `<div class="card"><span class="chip bad">Real-process mode could not start on this machine.</span></div>`;
  $('#result').innerHTML = h;
}

/* ---------- history ---------- */
async function history() {
  shell('history', `<h1>History</h1><p class="muted">Every run you saved. Open one to inspect or re-run it.</p><div class="card" id="hl">Loading…</div>`);
  const r = await api('runs'); if (!$('#hl')) return;
  $('#hl').innerHTML = !r.runs.length ? '<span class="muted">Nothing saved yet.</span>' : `<div class="tablewrap"><table><thead><tr><th>Title</th><th>Algorithm</th><th class="num">Procs</th><th class="num">Avg wait</th><th class="num">Worst wait (no aging → aging)</th><th>Date</th><th></th></tr></thead><tbody>
    ${r.runs.map(x => `<tr><td>${esc(x.title)}</td><td>${ALGO[x.algo]}${x.aging ? ' + aging' : ''}</td><td class="num">${x.nproc}</td><td class="num">${x.avg_wait.toFixed(2)}</td><td class="num">${x.mw_noaging} → ${x.mw_aging}</td><td class="small muted">${fmtDate(x.created)}</td>
    <td class="right" style="white-space:nowrap"><button class="btn sm" data-open="${x.id}">Open</button> <button class="btn ghost sm" data-rm="${x.id}">Delete</button></td></tr>`).join('')}</tbody></table></div>`;
  $('#hl').onclick = async e => {
    if (e.target.dataset.open) { const g = await api('runs/get?id=' + e.target.dataset.open); if (!g.ok) return toast(g.error, 1);
      const R = g.result; S.last = R; S.procs = R.input.split('\n').filter(Boolean).map(l => { const a = l.split(','); return { name: a[0], arrival: +a[1], burst: +a[2], prio: +a[3] }; });
      S.opts = { algo: R.opts.algo, aging: R.opts.aging, interval: R.opts.interval, step: R.opts.step, real: 0, tick: R.opts.tick_ms }; location.hash = '#/scheduler'; }
    if (e.target.dataset.rm && confirm('Delete this run?')) { await api('runs/delete', { id: e.target.dataset.rm }); toast('Run deleted'); history(); }
  };
}

/* ---------- profile ---------- */
function profile() {
  const u = S.user;
  const v = shell('profile', `<h1>Profile</h1><div class="grid g2"><div><div class="card"><div class="row" style="margin-bottom:.6rem"><div class="avatar lg">${esc(u.name.slice(0, 1).toUpperCase())}</div><div><h2 style="margin:0">${esc(u.name)}</h2><span class="muted small">Member since ${fmtDate(u.created)}</span></div></div>
    <form id="pf"><label>Full name</label><input name="name" value="${esc(u.name)}" required maxlength="60"><label>Email</label><input name="email" type="email" value="${esc(u.email)}" required>
    <label>Bio</label><textarea name="bio" rows="3" maxlength="300" placeholder="Tell us about yourself">${esc(u.bio)}</textarea><div class="err" id="perr"></div><button class="btn">Save profile</button></form></div></div>
    <div><div class="card"><h2>Change password</h2><form id="pw"><label>Current password</label><input name="old" type="password" required autocomplete="current-password"><label>New password</label><input name="new" type="password" required minlength="8" autocomplete="new-password">
    <div class="err" id="wer"></div><button class="btn">Update password</button><p class="muted small">Other signed-in devices will be logged out.</p></form></div>
    <div class="card" style="border-color:var(--bad)"><h2>Delete account</h2><p class="muted small">Permanently removes your account, settings and saved runs.</p><form id="dl"><input name="password" type="password" placeholder="Confirm with your password" required><div class="err" id="der"></div><button class="btn danger">Delete my account</button></form></div></div></div>`);
  $('#pf').onsubmit = async e => { e.preventDefault(); const r = await api('profile', Object.fromEntries(new FormData(e.target))); if (!r.ok) { $('#perr').textContent = r.error; return; } S.user = r.user; toast('Profile updated'); profile(); };
  $('#pw').onsubmit = async e => { e.preventDefault(); const r = await api('password', Object.fromEntries(new FormData(e.target))); if (!r.ok) { $('#wer').textContent = r.error; return; } e.target.reset(); $('#wer').textContent = ''; toast('Password changed'); };
  $('#dl').onsubmit = async e => { e.preventDefault(); if (!confirm('This cannot be undone. Delete your account?')) return; const r = await api('account/delete', Object.fromEntries(new FormData(e.target))); if (!r.ok) { $('#der').textContent = r.error; return; } S.user = null; S.procs = null; S.last = null; S.opts = null; location.hash = '#/signup'; };
}

/* ---------- settings ---------- */
function settings() {
  const s = S.settings;
  shell('settings', `<h1>Settings</h1><p class="muted">Defaults used every time you open the scheduler.</p><form id="sf" class="grid g2"><div class="card"><h2>Appearance</h2><label>Theme</label><select name="theme"><option value="dark">Dark</option><option value="light">Light</option></select></div>
    <div class="card"><h2>Scheduler defaults</h2><label>Algorithm</label><select name="algo"><option value="0">Priority — preemptive</option><option value="1">Priority — non-preemptive</option></select>
    <label class="check"><input type="checkbox" name="aging"> Aging enabled by default</label><div class="grid g2"><div><label>Aging interval (ticks)</label><input name="aging_interval" type="number" min="1" max="50"></div><div><label>Aging step</label><input name="aging_step" type="number" min="1" max="20"></div></div>
    <label class="check"><input type="checkbox" name="real_mode"> Use real Linux processes by default</label><label>Tick length (ms)</label><input name="tick_ms" type="number" min="20" max="500"></div>
    <div><div class="err" id="ser"></div><button class="btn">Save settings</button></div></form>`);
  const f = $('#sf'); f.theme.value = s.theme; f.algo.value = s.algo; f.aging.checked = !!s.aging; f.aging_interval.value = s.aging_interval; f.aging_step.value = s.aging_step; f.real_mode.checked = !!s.real_mode; f.tick_ms.value = s.tick_ms;
  f.theme.onchange = () => setTheme(f.theme.value);
  f.onsubmit = async e => { e.preventDefault(); const d = Object.fromEntries(new FormData(f)); d.aging = f.aging.checked ? 1 : 0; d.real_mode = f.real_mode.checked ? 1 : 0;
    const r = await api('settings', d); if (!r.ok) { $('#ser').textContent = r.error; return; } S.settings = r.settings; S.opts = null; setTheme(S.settings.theme); toast('Settings saved'); };
}

/* ---------- about ---------- */
function about() {
  shell('about', `<h1>About the project</h1><p class="muted">Operating System and System Programming (25CS2104E) — Priority Scheduling · Review-I</p>
  <div class="card"><h2>Abstract</h2><p>A Linux-based priority scheduling system that shows how an operating system manages processes competing for the CPU. Plain priority scheduling can <b>starve</b> low-priority processes; this project adds an <b>aging</b> mechanism that gradually raises the priority of waiting processes. It is written in C using POSIX/Linux mechanisms — <code>fork()</code>, <code>exec()</code>, <code>waitpid()</code>, <code>kill()</code>, <code>setpriority()</code>, signals and pipes — and reports waiting time, turnaround time, response time, CPU utilisation and context switches.</p></div>
  <div class="card"><h2>System architecture</h2><div class="flow"><div class="box"><b>Process Generator</b><span class="small muted">creates N processes with priorities</span></div><span class="arrow">→</span>
    <div class="box"><b>Priority Ready Queue</b><span class="small muted">ordered by current priority</span></div><span class="arrow">→</span><div class="box"><b>Scheduler + Aging</b><span class="small muted">picks highest priority, ages waiters</span></div><span class="arrow">→</span>
    <div class="box"><b>CPU Executor</b><span class="small muted">runs via exec(), tracks time</span></div><span class="arrow">→</span><div class="box"><b>Metrics Collector</b><span class="small muted">waiting, turnaround, response</span></div></div>
    <p class="muted small" style="margin-top:.8rem">SIGCONT/SIGSTOP (via <code>kill()</code>) and pipes provide IPC between the scheduler and running processes; aging feeds priority updates back into the ready queue every cycle. This web app is built in C too: a multi-threaded HTTP server, SQLite database, and this single-page frontend.</p></div>
  <div class="grid g2"><div class="card"><h2>Requirements</h2><table><tbody><tr><td>OS</td><td>Linux (Ubuntu 20.04+) / WSL</td></tr><tr><td>Language</td><td>C (C11 / GNU11)</td></tr><tr><td>Build</td><td>GCC, Make</td></tr><tr><td>APIs</td><td>fork, exec, waitpid, kill, setpriority, signals, pipes</td></tr><tr><td>Tools</td><td>GDB, Valgrind, strace</td></tr><tr><td>Hardware</td><td>Dual-core CPU, 2 GB RAM, 1 GB disk</td></tr></tbody></table></div>
    <div class="card"><h2>Future work</h2><ul class="muted"><li>Multi-level feedback queues</li><li>Multi-core load balancing</li><li>Live visualisation of process states over time</li></ul><h3>Team</h3><p class="muted small">Guide: Raghupathi M (CSE)<br>Varshith Comple · Mohith Karthik · Satya Rishith</p></div></div>`);
}

/* ---------- router ---------- */
const ROUTES = { dashboard, scheduler, history, profile, settings, about };
async function route() {
  const name = (location.hash.replace(/^#\//, '') || 'dashboard').split('?')[0];
  if (S.user === null) { const me = await api('me'); if (me.ok) { S.user = me.user; S.settings = me.settings; setTheme(S.settings.theme); } else S.user = false; }
  if (!S.user) { if (name !== 'signup') { if (name !== 'login') { location.hash = '#/login'; return; } } return authView(name === 'signup' ? 'signup' : 'login'); }
  if (name === 'login' || name === 'signup') { location.hash = '#/dashboard'; return; }
  (ROUTES[name] || dashboard)();
}
window.addEventListener('hashchange', route); route();
