const express = require('express');
const db = require('../db');
const { requireAuth } = require('../middleware/auth');

const router = express.Router();
router.use(requireAuth); // every saved run belongs to the logged-in user

function toRun(row) {
  return {
    id: row.id,
    title: row.title,
    config: JSON.parse(row.config),
    result: JSON.parse(row.result),
    created_at: row.created_at
  };
}

// GET /api/runs
router.get('/', (req, res) => {
  const rows = db.prepare('SELECT * FROM scheduler_runs WHERE user_id = ? ORDER BY id DESC').all(req.user.id);
  res.json({ runs: rows.map(toRun) });
});

// POST /api/runs
router.post('/', (req, res) => {
  const title = String(req.body.title || '').trim().slice(0, 80) || 'Untitled run';
  const { config, result } = req.body;
  if (!config || !result) return res.status(400).json({ error: 'Run data is missing.' });

  const info = db
    .prepare('INSERT INTO scheduler_runs (user_id, title, config, result) VALUES (?, ?, ?, ?)')
    .run(req.user.id, title, JSON.stringify(config), JSON.stringify(result));
  const row = db.prepare('SELECT * FROM scheduler_runs WHERE id = ?').get(info.lastInsertRowid);
  res.status(201).json({ run: toRun(row) });
});

// DELETE /api/runs/:id
router.delete('/:id', (req, res) => {
  const info = db.prepare('DELETE FROM scheduler_runs WHERE id = ? AND user_id = ?').run(req.params.id, req.user.id);
  if (!info.changes) return res.status(404).json({ error: 'Run not found.' });
  res.json({ ok: true });
});

module.exports = router;
