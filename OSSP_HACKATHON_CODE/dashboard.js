// Dashboard: protected page with the scheduler simulator and saved runs.
const DEFAULT_TASKS = [
  { name: 'Video call', type: 'io', base: 5, burst: 6 },
  { name: 'Math job', type: 'cpu', base: 3, burst: 10 },
  { name: 'Backup', type: 'cpu', base: 1, burst: 6 }
];

const rowsEl = document.getElementById('proc-rows');
let lastRun = null;

function addRow(task = { name: '', type: 'cpu', base: 1, burst: 4 }) {
  const row = h('tr');
  const nameInput = h('input', { class: 'input-cell', value: task.name, maxlength: '20', placeholder: 'Task name', 'aria-label': 'Task name' });
  const typeSelect = h('select', { class: 'input-cell', 'aria-label': 'Task type' }, [
    h('option', { value: 'cpu', text: 'CPU' }),
    h('option', { value: 'io', text: 'I/O' })
  ]);
  typeSelect.value = task.type;
  const baseInput = h('input', { class: 'input-cell', type: 'number', min: '0', max: '20', value: String(task.base), 'aria-label': 'Base priority' });
  const burstInput = h('input', { class: 'input-cell', type: 'number', min: '1', max: '30', value: String(task.burst), 'aria-label': 'Work in ticks' });
  const removeBtn = h('button', { class: 'icon-btn', type: 'button', text: '×', 'aria-label': 'Remove task', onclick: () => row.remove() });

  [nameInput, typeSelect, baseInput, burstInput, removeBtn].forEach(el => row.append(h('td', {}, [el])));
  rowsEl.append(row);
}

function readTasks() {
  const rows = [...document.querySelectorAll('#proc-rows tr')];
  if (!rows.length) throw new Error('Add at least one task.');
  if (rows.length > 8) throw new Error('Use up to 8 tasks.');

  const seen = new Set();
  return rows.map((tr, i) => {
    const [nameEl, typeEl, baseEl, burstEl] = tr.querySelectorAll('input, select');
    const name = nameEl.value.trim();
    if (!name) throw new Error(`Task ${i + 1} needs a name.`);
    if (seen.has(name)) throw new Error(`Task names must be unique. "${name}" appears more than once.`);
    seen.add(name);

    const base = Number(baseEl.value);
    const burst = Number(burstEl.value);
    if (!Number.isInteger(base) || base < 0 || base > 20) throw new Error(`Base priority for "${name}" must be a whole number from 0 to 20.`);
    if (!Number.isInteger(burst) || burst < 1 || burst > 30) throw new Error(`Work for "${name}" must be a whole number from 1 to 30.`);

    return { name, type: typeEl.value, base, burst };
  });
}

function formatDate(value) {
  const date = new Date(String(value).replace(' ', 'T') + 'Z');
  return isNaN(date) ? value : date.toLocaleString();
}

function gantt(timeline, names) {
  const cols = Math.max(timeline.length, 1);
  const grid = h('div', { class: 'gantt', style: `grid-template-columns: 90px repeat(${cols}, minmax(18px, 1fr))` });
  grid.append(h('div'));
  timeline.forEach(t => grid.append(h('div', { class: 'gantt-tick', text: String(t.tick) })));
  names.forEach(name => {
    grid.append(h('div', { class: 'gantt-label', text: name }));
    timeline.forEach(t => {
      grid.append(h('div', {
        class: 'gantt-cell' + (t.running === name ? ' on' : ''),
        title: `${name}: tick ${t.tick}${t.running === name ? ' (running)' : ''}`
      }));
    });
  });
  return grid;
}

function renderResults(run) {
  const box = document.getElementById('results');
  box.innerHTML = '';

  const { before, after } = run.result;
  const names = run.config.tasks.map(t => t.name);
  const fmtFinish = t => (t == null ? 'Not finished' : `Tick ${t}`);

  const rows = names.map(name => {
    const b = before.summary.find(s => s.name === name);
    const a = after.summary.find(s => s.name === name);
    return h('tr', {}, [
      h('td', { text: name }),
      h('td', { text: String(b.base) }),
      h('td', { text: String(b.waitTicks) }),
      h('td', { text: String(a.waitTicks) }),
      h('td', { text: fmtFinish(b.finishTick) }),
      h('td', { text: fmtFinish(a.finishTick) })
    ]);
  });

  box.append(
    h('table', { class: 'summary-table' }, [
      h('thead', {}, [h('tr', {}, ['Task', 'Base', 'Waiting before', 'Waiting after', 'Done before', 'Done after'].map(t => h('th', { text: t })))]),
      h('tbody', {}, rows)
    ]),
    h('p', { class: 'note', text: 'Waiting is the number of ticks a task was ready but another task had the CPU. Lower is better for tasks that would otherwise wait a long time.' }),
    h('div', { class: 'gantt-wrap' }, [
      h('h3', { text: 'Base priority only (before)' }),
      gantt(before.timeline, names),
      h('h3', { text: 'Dynamic priority (after)' }),
      gantt(after.timeline, names)
    ])
  );
}

function runSimulation() {
  const errorBox = document.getElementById('run-error');
  errorBox.hidden = true;
  try {
    const tasks = readTasks();
    const before = Scheduler.simulate(tasks, { dynamic: false });
    const after = Scheduler.simulate(tasks, { dynamic: true });
    lastRun = { config: { tasks }, result: { before, after } };
    renderResults(lastRun);
    document.getElementById('save-row').hidden = false;
  } catch (error) {
    errorBox.textContent = error.message;
    errorBox.hidden = false;
  }
}

async function saveRun() {
  if (!lastRun) return;
  const input = document.getElementById('run-title');
  const errorBox = document.getElementById('run-error');
  const button = document.getElementById('save-btn');
  const title = input.value.trim() || `Run on ${new Date().toLocaleString()}`;

  button.disabled = true;
  try {
    await api('/runs', { method: 'POST', body: { title, config: lastRun.config, result: lastRun.result } });
    input.value = '';
    errorBox.hidden = true;
    await loadSavedRuns();
  } catch (error) {
    errorBox.textContent = error.message;
    errorBox.hidden = false;
  } finally {
    button.disabled = false;
  }
}

function loadRun(run) {
  lastRun = { config: run.config, result: run.result };
  rowsEl.innerHTML = '';
  run.config.tasks.forEach(addRow);
  renderResults(lastRun);
  document.getElementById('save-row').hidden = true;
  document.getElementById('results').scrollIntoView({ behavior: 'smooth', block: 'start' });
}

async function deleteRun(id) {
  if (!confirm('Delete this saved run?')) return;
  try {
    await api(`/runs/${id}`, { method: 'DELETE' });
    await loadSavedRuns();
  } catch (error) {
    alert(error.message);
  }
}

function savedRunItem(run) {
  const total = list => list.reduce((sum, s) => sum + s.waitTicks, 0);
  const before = total(run.result.before.summary);
  const after = total(run.result.after.summary);

  return h('div', { class: 'run-item' }, [
    h('div', {}, [
      h('strong', { text: run.title }),
      h('div', { class: 'muted small', text: `Saved ${formatDate(run.created_at)}. Total waiting: ${before} before, ${after} after.` })
    ]),
    h('div', { class: 'run-actions' }, [
      h('button', { class: 'btn btn-ghost', type: 'button', text: 'Load', onclick: () => loadRun(run) }),
      h('button', { class: 'btn btn-ghost', type: 'button', text: 'Delete', onclick: () => deleteRun(run.id) })
    ])
  ]);
}

async function loadSavedRuns() {
  const list = document.getElementById('saved-list');
  try {
    const { runs } = await api('/runs');
    list.innerHTML = '';
    if (!runs.length) {
      list.append(h('p', { class: 'muted', text: 'No saved runs yet. Run the simulation, then click Save run.' }));
      return;
    }
    runs.forEach(run => list.append(savedRunItem(run)));
  } catch (error) {
    list.innerHTML = '';
    list.append(h('p', { class: 'form-error', text: error.message }));
  }
}

(async function initDashboard() {
  try {
    const { user } = await api('/auth/me');
    document.getElementById('user-name').textContent = user.name;
  } catch (error) {
    clearSession();
    location.href = 'login.html';
    return;
  }

  DEFAULT_TASKS.forEach(addRow);
  document.getElementById('add-row').addEventListener('click', () => addRow());
  document.getElementById('run-btn').addEventListener('click', runSimulation);
  document.getElementById('save-btn').addEventListener('click', saveRun);
  loadSavedRuns();
})();
