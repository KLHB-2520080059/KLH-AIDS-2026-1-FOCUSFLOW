# Smart CPU Scheduler: Website

A full-stack website for the Smart CPU Scheduler project. It has a landing page, sign up, log in, a protected dashboard with a scheduler simulator, and a database that saves each user's runs.

## What is inside

```
cpu-scheduler-website/
├── frontend/          Web pages (HTML, CSS, JavaScript)
│   ├── index.html       Landing page
│   ├── signup.html      Sign up
│   ├── login.html       Log in
│   ├── dashboard.html   Scheduler simulator (login required)
│   ├── css/style.css
│   └── js/              api.js, main.js, auth.js, index.js, scheduler.js, dashboard.js
├── backend/           Server (Node.js + Express)
│   ├── server.js        Starts the server and connects everything
│   ├── db.js            Connects to the database
│   ├── middleware/auth.js
│   ├── routes/          auth.js, runs.js, syllabus.js
│   ├── package.json     Dependencies and start commands
│   └── .env.example     Optional settings
└── database/          SQLite database
    └── schema.sql       Table definitions (app.db is created automatically on first run)
```

## Run it on a Mac

### 1. Install Node.js (one time)

Check whether Node is already installed. Open Terminal (Cmd + Space, type "Terminal") and run:

```bash
node -v
```

If you see a version number 18 or higher, skip to step 2. If not, do one of these:

- **Option A:** Download the LTS installer from https://nodejs.org and run it.
- **Option B (Homebrew):** `brew install node`

Run `node -v` again to confirm.

### 2. Unzip the project

Double-click `cpu-scheduler-website.zip`. A folder named `cpu-scheduler-website` appears.

### 3. Open the backend folder in Terminal

Type `cd ` (with a space), drag the `backend` folder from Finder into the Terminal window, and press Enter. Or type the full path:

```bash
cd ~/Downloads/cpu-scheduler-website/backend
```

Change `~/Downloads` if you unzipped it somewhere else.

### 4. Install the dependencies (one time)

```bash
npm install
```

This downloads the libraries listed in `package.json` into a `node_modules` folder. It takes about a minute.

> If you see an error about "better-sqlite3" or a missing compiler, run `xcode-select --install`, accept the prompt, wait for it to finish, then run `npm install` again.

### 5. Start the server

```bash
npm start
```

You should see:

```
Smart CPU Scheduler is running at http://localhost:3000
```

Keep this Terminal window open while you use the site. Stop the server any time with `Ctrl + C`.

### 6. Open the website

Go to **http://localhost:3000** in your browser.

1. Click **Sign up**, create an account, and you land on the dashboard.
2. Use the simulator: edit the tasks, click **Run simulation**, then **Save run**.
3. Click **Log out** to end the session, then **Log in** to come back.

## Optional settings

Copy `backend/.env.example` to `backend/.env` and change the values:

| Setting | What it does |
|---|---|
| `PORT` | Port number (default 3000). Use another port if 3000 is busy. |
| `JWT_SECRET` | Secret used to sign login tokens. Set a long random string before sharing the site. |

To use a different port without a file, run: `PORT=4000 npm start`

## Where is my data?

Users and saved runs are stored in `database/app.db`. Delete that file to reset everything. The tables are created again on the next start.

## API endpoints

| Method | Path | Login needed | Purpose |
|---|---|---|---|
| POST | `/api/auth/signup` | No | Create an account |
| POST | `/api/auth/login` | No | Log in and receive a token |
| GET | `/api/auth/me` | Yes | Get the current user |
| GET | `/api/syllabus` | No | Course outcomes shown on the landing page |
| GET | `/api/runs` | Yes | List your saved runs |
| POST | `/api/runs` | Yes | Save a simulation run |
| DELETE | `/api/runs/:id` | Yes | Delete one of your saved runs |

## Troubleshooting

| Problem | Fix |
|---|---|
| `command not found: npm` | Install Node.js (step 1), then open a new Terminal window. |
| `EADDRINUSE: port 3000 already in use` | Run `PORT=4000 npm start`, then open http://localhost:4000. |
| `better-sqlite3` build error | Run `xcode-select --install`, then `npm install` again. |
| Page says "Could not load the syllabus" | The server is not running. Go back to Terminal and run `npm start`. |
| Forgot your test password | Delete `database/app.db` and sign up again (this also deletes all accounts and runs). |
