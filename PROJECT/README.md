# PrioSched — Priority Scheduling with Aging (C web app)

OSSP (25CS2104E) project as a full website. **Everything is written in C**: HTTP server, REST API, scheduler engine and SQLite database layer (SQLite is bundled in `vendor/`, nothing to install except GCC + Make).

## Run it (Linux / Ubuntu / WSL / macOS (Xcode command line tools: xcode-select --install))

```bash
unzip ossp-priority-scheduler.zip
cd ossp-priority-scheduler
./run.sh
```

The first run compiles everything (~30–60 s, mostly SQLite), then your browser opens at **http://localhost:8080**.
If no browser opens, open that URL manually. Stop with `Ctrl+C`.

Missing compiler? `sudo apt update && sudo apt install -y build-essential`
Windows: use **WSL (Ubuntu)** and run the same commands; open the URL in your Windows browser.

## Features
- **Sign up / Log in / Log out** — PBKDF2-SHA256 salted password hashes, HttpOnly session cookies, login rate-limiting
- **Dashboard** — saved runs, processes scheduled, average wait, how much aging cut the worst wait
- **Scheduler** — preemptive & non-preemptive priority scheduling, **aging** (interval + step), Gantt charts, waiting/turnaround/response time, CPU utilisation, context switches, throughput, with-vs-without-aging comparison, starvation demo preset
- **Real-process mode** — runs the schedule on real Linux processes: `fork()` + `exec()`, `SIGSTOP`/`SIGCONT` via `kill()`, `setpriority()`, `waitpid()`, pipes for status reporting
- **History** — save, reopen and delete runs
- **Profile** — edit name/email/bio, change password, delete account
- **Settings** — dark/light theme, default algorithm, aging parameters, real-mode defaults
- **About** — abstract, architecture and requirements from the project review

## Layout
```
run.sh  Makefile  README.md
src/server.c      HTTP server, routes, sessions, SQLite queries
src/sched.c       scheduler + aging engine, real-process executor
src/crypto.c      SHA-256 / HMAC / PBKDF2
vendor/sqlite3.*  bundled SQLite amalgamation
www/              index.html, style.css, app.js (single-page frontend)
data/ossp.db      created automatically on first run (delete it to reset everything)
```

## Options (environment variables)
`PORT=9000 ./run.sh` · `NO_BROWSER=1 ./run.sh` · `OSSP_PUBLIC=1 ./run.sh` (listen on all interfaces; default is localhost only)

## API (all JSON)
`POST /api/signup|login|logout|profile|password|settings|simulate|runs/delete|account/delete` · `GET /api/me|runs|runs/get?id=|stats`

## Notes
- Lower priority number = higher priority (like Linux `nice`).
- `setpriority()` can lower a process's priority freely, but raising it back needs root; those attempts are reported as "denied" in the real-mode panel.
- Limits: 20 processes, total burst ≤ 600 ticks; real mode ≤ 150 ticks and ~20 s.
