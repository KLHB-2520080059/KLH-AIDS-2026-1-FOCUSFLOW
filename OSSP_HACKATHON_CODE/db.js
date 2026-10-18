const path = require('path');
const fs = require('fs');
// Built into Node.js 22.13+ (no compiling needed)
const { DatabaseSync } = require('node:sqlite');

// The database file is stored in ../database/app.db
const DB_PATH = process.env.DB_PATH || path.join(__dirname, '..', 'database', 'app.db');
fs.mkdirSync(path.dirname(DB_PATH), { recursive: true });

const db = new DatabaseSync(DB_PATH);
db.exec('PRAGMA journal_mode = WAL;');
db.exec('PRAGMA foreign_keys = ON;');

// Create the tables if they do not exist yet
const schema = fs.readFileSync(path.join(__dirname, '..', 'database', 'schema.sql'), 'utf8');
db.exec(schema);

module.exports = db;
