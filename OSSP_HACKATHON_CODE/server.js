const path = require('path');
const express = require('express');
require('./db'); // opens the database and creates the tables

const authRoutes = require('./routes/auth');
const runRoutes = require('./routes/runs');
const syllabusRoutes = require('./routes/syllabus');

const app = express();
const PORT = process.env.PORT || 3000;
const FRONTEND_DIR = path.join(__dirname, '..', 'frontend');

app.use(express.json({ limit: '500kb' }));

// API routes
app.use('/api/auth', authRoutes);
app.use('/api/runs', runRoutes);
app.use('/api/syllabus', syllabusRoutes);
app.use('/api', (req, res) => res.status(404).json({ error: 'Route not found.' }));

// Frontend (HTML, CSS, JS) served from ../frontend
app.use(express.static(FRONTEND_DIR));

// Error handler
app.use((err, req, res, next) => {
  console.error(err);
  res.status(err.status || 500).json({
    error: err.status ? err.message : 'Something went wrong on the server.'
  });
});

app.listen(PORT, () => {
  console.log(`Smart CPU Scheduler is running at http://localhost:${PORT}`);
});
