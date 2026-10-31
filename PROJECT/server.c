#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "sqlite3.h"
#include "crypto.h"
#include "pscheduler.h"

#define PBKDF_ITER 30000
#define SESSION_SECS (7 * 24 * 3600)
#define MAX_BODY 32768
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 /* macOS: SIGPIPE is ignored globally instead */
#endif

/* ---------------- growable buffer + JSON helpers ---------------- */
typedef struct { char *p; size_t n, cap; } Buf;
static void bcat(Buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void bputs(Buf *b, const char *s) { bcat(b, s, strlen(s)); }
static void bf(Buf *b, const char *fmt, ...) {
    va_list ap, ap2; va_start(ap, fmt); va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    if (n > 0) { char *t = malloc(n + 1); vsnprintf(t, n + 1, fmt, ap2); bcat(b, t, n); free(t); }
    va_end(ap2);
}
static void bstr(Buf *b, const char *s) {
    bputs(b, "\""); if (!s) s = "";
    for (; *s; s++) {
        unsigned char c = *s;
        if (c == '"') bputs(b, "\\\""); else if (c == '\\') bputs(b, "\\\\");
        else if (c == '\n') bputs(b, "\\n"); else if (c == '\r') bputs(b, "\\r"); else if (c == '\t') bputs(b, "\\t");
        else if (c < 0x20) bf(b, "\\u%04x", c); else bcat(b, (char *)&c, 1);
    }
    bputs(b, "\"");
}

/* ---------------- globals ---------------- */
static sqlite3 *DB;
static pthread_mutex_t M = PTHREAD_MUTEX_INITIALIZER;
static int real_runs = 0;

static sqlite3_stmt *Q(const char *sql) {
    sqlite3_stmt *s = NULL;
    if (sqlite3_prepare_v2(DB, sql, -1, &s, 0) != SQLITE_OK) { fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(DB)); return NULL; }
    return s;
}
static const char *T(sqlite3_stmt *s, int i) { const char *c = (const char *)sqlite3_column_text(s, i); return c ? c : ""; }

/* ---------------- request / response ---------------- */
typedef struct { int fd; char method[8], path[1100], query[1100], cookie[600]; int xhr; char *body; size_t blen; } Req;

static void send_all(int fd, const char *p, size_t n) {
    while (n) { ssize_t k = send(fd, p, n, MSG_NOSIGNAL); if (k <= 0) return; p += k; n -= k; }
}
static void respond(Req *r, int code, const char *ctype, const char *extra, const char *body, size_t n) {
    const char *st = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 401 ? "Unauthorized" : code == 404 ? "Not Found" :
                     code == 409 ? "Conflict" : code == 429 ? "Too Many Requests" : code == 503 ? "Service Unavailable" : "Error";
    char h[1024];
    int hl = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n"
                      "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\n%s\r\n",
                      code, st, ctype, n, extra ? extra : "");
    send_all(r->fd, h, hl); send_all(r->fd, body, n);
}
static void jerr(Req *r, int code, const char *msg) {
    Buf b = {0}; bputs(&b, "{\"ok\":false,\"error\":"); bstr(&b, msg); bputs(&b, "}");
    respond(r, code, "application/json", NULL, b.p, b.n); free(b.p);
}
static void jsend(Req *r, Buf *b, const char *extra) { respond(r, 200, "application/json", extra, b->p, b->n); }
static void jok(Req *r, const char *extra) { respond(r, 200, "application/json", extra, "{\"ok\":true}", 11); }

/* ---------------- form parsing ---------------- */
static int form_get(const char *body, const char *key, char *out, size_t sz) {
    size_t kl = strlen(key); const char *p = body; out[0] = 0;
    while (p && *p) {
        const char *amp = strchr(p, '&'); size_t len = amp ? (size_t)(amp - p) : strlen(p);
        if (len > kl && !strncmp(p, key, kl) && p[kl] == '=') {
            const char *v = p + kl + 1, *e = p + len; size_t o = 0;
            while (v < e && o < sz - 1) {
                if (*v == '%' && e - v >= 3 && isxdigit((unsigned char)v[1]) && isxdigit((unsigned char)v[2])) {
                    char h[3] = {v[1], v[2], 0}; out[o++] = (char)strtol(h, 0, 16); v += 3;
                } else if (*v == '+') { out[o++] = ' '; v++; } else out[o++] = *v++;
            }
            out[o] = 0; return 1;
        }
        p = amp ? amp + 1 : NULL;
    }
    return 0;
}
static int form_int(const char *body, const char *key, int def) {
    char t[32]; if (!form_get(body, key, t, sizeof t) || !t[0]) return def; return atoi(t);
}
static void trim(char *s) {
    size_t n = strlen(s); while (n && isspace((unsigned char)s[n-1])) s[--n] = 0;
    char *p = s; while (*p && isspace((unsigned char)*p)) p++; if (p != s) memmove(s, p, strlen(p) + 1);
}
static int valid_email(const char *e) {
    size_t n = strlen(e); if (n < 5 || n > 120) return 0;
    const char *at = strchr(e, '@'); if (!at || at == e || strchr(at + 1, '@')) return 0;
    if (!strchr(at + 1, '.') || at[1] == '.' || e[n-1] == '.') return 0;
    for (const char *c = e; *c; c++) if (isspace((unsigned char)*c) || *c == '"' || *c == '<' || *c == '>') return 0;
    return 1;
}

/* ---------------- login rate limiting ---------------- */
static struct { unsigned h; int cnt; time_t first; } RL[64]; static int rl_next;
static unsigned fnv(const char *s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }
static int rl_blocked(const char *k) {
    unsigned h = fnv(k); time_t now = time(0);
    for (int i = 0; i < 64; i++) if (RL[i].h == h && RL[i].cnt >= 5 && now - RL[i].first < 300) return 1;
    return 0;
}
static void rl_fail(const char *k) {
    unsigned h = fnv(k); time_t now = time(0);
    for (int i = 0; i < 64; i++) if (RL[i].h == h) { if (now - RL[i].first > 300) { RL[i].cnt = 0; RL[i].first = now; } RL[i].cnt++; return; }
    RL[rl_next].h = h; RL[rl_next].cnt = 1; RL[rl_next].first = now; rl_next = (rl_next + 1) % 64;
}
static void rl_clear(const char *k) { unsigned h = fnv(k); for (int i = 0; i < 64; i++) if (RL[i].h == h) RL[i].cnt = 0; }

/* ---------------- sessions / users ---------------- */
static int token_from_cookie(Req *r, char *tok) {
    const char *c = r->cookie;
    while ((c = strstr(c, "sid=")) != NULL) {
        if (c == r->cookie || c[-1] == ' ' || c[-1] == ';') {
            c += 4; int i = 0; while (i < 64 && isxdigit((unsigned char)c[i])) { tok[i] = c[i]; i++; }
            tok[i] = 0; return i == 64;
        }
        c += 4;
    }
    return 0;
}
static int cur_user(Req *r) {
    char tok[70]; if (!token_from_cookie(r, tok)) return 0;
    sqlite3_stmt *s = Q("SELECT user_id FROM sessions WHERE token=? AND expires>?");
    sqlite3_bind_text(s, 1, tok, -1, SQLITE_STATIC); sqlite3_bind_int64(s, 2, time(0));
    int uid = sqlite3_step(s) == SQLITE_ROW ? sqlite3_column_int(s, 0) : 0;
    sqlite3_finalize(s); return uid;
}
static void new_session(int uid, char *cookie_hdr, size_t sz) {
    uint8_t rb[32]; char tok[65]; rand_bytes(rb, 32); to_hex(rb, 32, tok);
    sqlite3_exec(DB, "DELETE FROM sessions WHERE expires<strftime('%s','now')", 0, 0, 0);
    sqlite3_stmt *s = Q("INSERT INTO sessions(token,user_id,expires) VALUES(?,?,?)");
    sqlite3_bind_text(s, 1, tok, -1, SQLITE_STATIC); sqlite3_bind_int(s, 2, uid); sqlite3_bind_int64(s, 3, time(0) + SESSION_SECS);
    sqlite3_step(s); sqlite3_finalize(s);
    snprintf(cookie_hdr, sz, "Set-Cookie: sid=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=%d\r\n", tok, SESSION_SECS);
}
static void hash_pw(const char *pw, const char *salt_hex, char *out_hex) {
    uint8_t salt[16], h[32];
    for (int i = 0; i < 16; i++) { char t[3] = {salt_hex[2*i], salt_hex[2*i+1], 0}; salt[i] = (uint8_t)strtol(t, 0, 16); }
    pbkdf2_sha256(pw, salt, 16, PBKDF_ITER, h); to_hex(h, 32, out_hex);
}
static void make_salt(char *salt_hex) { uint8_t s[16]; rand_bytes(s, 16); to_hex(s, 16, salt_hex); }
static int check_pw(int uid, const char *pw) {
    sqlite3_stmt *s = Q("SELECT salt,hash FROM users WHERE id=?"); sqlite3_bind_int(s, 1, uid);
    int ok = 0;
    if (sqlite3_step(s) == SQLITE_ROW) { char h[65]; hash_pw(pw, T(s, 0), h); ok = ct_eq(h, T(s, 1), 64); }
    sqlite3_finalize(s); return ok;
}

static void json_me(Buf *b, int uid) {
    sqlite3_stmt *s = Q("SELECT u.name,u.email,u.bio,u.created,s.theme,s.algo,s.aging,s.aging_interval,s.aging_step,s.tick_ms,s.real_mode "
                        "FROM users u JOIN settings s ON s.user_id=u.id WHERE u.id=?");
    sqlite3_bind_int(s, 1, uid);
    if (sqlite3_step(s) == SQLITE_ROW) {
        bf(b, "{\"ok\":true,\"user\":{\"id\":%d,\"name\":", uid); bstr(b, T(s, 0));
        bputs(b, ",\"email\":"); bstr(b, T(s, 1)); bputs(b, ",\"bio\":"); bstr(b, T(s, 2));
        bf(b, ",\"created\":%lld},\"settings\":{\"theme\":", (long long)sqlite3_column_int64(s, 3)); bstr(b, T(s, 4));
        bf(b, ",\"algo\":%d,\"aging\":%d,\"aging_interval\":%d,\"aging_step\":%d,\"tick_ms\":%d,\"real_mode\":%d}}",
           sqlite3_column_int(s, 5), sqlite3_column_int(s, 6), sqlite3_column_int(s, 7), sqlite3_column_int(s, 8),
           sqlite3_column_int(s, 9), sqlite3_column_int(s, 10));
    } else bputs(b, "{\"ok\":false,\"error\":\"User not found\"}");
    sqlite3_finalize(s);
}

/* ---------------- auth routes ---------------- */
static void r_signup(Req *r) {
    char name[128], email[160], pw[160];
    form_get(r->body, "name", name, sizeof name); form_get(r->body, "email", email, sizeof email); form_get(r->body, "password", pw, sizeof pw);
    trim(name); trim(email); for (char *c = email; *c; c++) *c = tolower((unsigned char)*c);
    if (!name[0] || strlen(name) > 60) return jerr(r, 400, "Name is required (max 60 characters).");
    if (!valid_email(email)) return jerr(r, 400, "Please enter a valid email address.");
    if (strlen(pw) < 8 || strlen(pw) > 100) return jerr(r, 400, "Password must be 8-100 characters.");
    char salt[33], hash[65]; make_salt(salt); hash_pw(pw, salt, hash);
    sqlite3_stmt *s = Q("INSERT INTO users(name,email,salt,hash,created) VALUES(?,?,?,?,?)");
    sqlite3_bind_text(s, 1, name, -1, SQLITE_STATIC); sqlite3_bind_text(s, 2, email, -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 3, salt, -1, SQLITE_STATIC); sqlite3_bind_text(s, 4, hash, -1, SQLITE_STATIC); sqlite3_bind_int64(s, 5, time(0));
    int rc = sqlite3_step(s); sqlite3_finalize(s);
    if (rc != SQLITE_DONE) return jerr(r, 409, "An account with this email already exists.");
    int uid = (int)sqlite3_last_insert_rowid(DB);
    s = Q("INSERT INTO settings(user_id) VALUES(?)"); sqlite3_bind_int(s, 1, uid); sqlite3_step(s); sqlite3_finalize(s);
    char ck[256]; new_session(uid, ck, sizeof ck);
    Buf b = {0}; json_me(&b, uid); jsend(r, &b, ck); free(b.p);
}
static void r_login(Req *r) {
    char email[160], pw[160];
    form_get(r->body, "email", email, sizeof email); form_get(r->body, "password", pw, sizeof pw);
    trim(email); for (char *c = email; *c; c++) *c = tolower((unsigned char)*c);
    if (rl_blocked(email)) return jerr(r, 429, "Too many failed attempts. Try again in a few minutes.");
    sqlite3_stmt *s = Q("SELECT id FROM users WHERE email=?"); sqlite3_bind_text(s, 1, email, -1, SQLITE_STATIC);
    int uid = sqlite3_step(s) == SQLITE_ROW ? sqlite3_column_int(s, 0) : 0; sqlite3_finalize(s);
    int ok = 0;
    if (uid) ok = check_pw(uid, pw); else { char d[65]; hash_pw(pw, "00000000000000000000000000000000", d); }
    if (!ok) { rl_fail(email); return jerr(r, 401, "Incorrect email or password."); }
    rl_clear(email);
    char ck[256]; new_session(uid, ck, sizeof ck);
    Buf b = {0}; json_me(&b, uid); jsend(r, &b, ck); free(b.p);
}
static void r_logout(Req *r) {
    char tok[70];
    if (token_from_cookie(r, tok)) { sqlite3_stmt *s = Q("DELETE FROM sessions WHERE token=?"); sqlite3_bind_text(s, 1, tok, -1, SQLITE_STATIC); sqlite3_step(s); sqlite3_finalize(s); }
    jok(r, "Set-Cookie: sid=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0\r\n");
}
static void r_profile(Req *r, int uid) {
    char name[128], email[160], bio[600];
    form_get(r->body, "name", name, sizeof name); form_get(r->body, "email", email, sizeof email); form_get(r->body, "bio", bio, sizeof bio);
    trim(name); trim(email); trim(bio); for (char *c = email; *c; c++) *c = tolower((unsigned char)*c);
    if (!name[0] || strlen(name) > 60) return jerr(r, 400, "Name is required (max 60 characters).");
    if (!valid_email(email)) return jerr(r, 400, "Please enter a valid email address.");
    if (strlen(bio) > 300) return jerr(r, 400, "Bio is limited to 300 characters.");
    sqlite3_stmt *s = Q("UPDATE users SET name=?,email=?,bio=? WHERE id=?");
    sqlite3_bind_text(s, 1, name, -1, SQLITE_STATIC); sqlite3_bind_text(s, 2, email, -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 3, bio, -1, SQLITE_STATIC); sqlite3_bind_int(s, 4, uid);
    int rc = sqlite3_step(s); sqlite3_finalize(s);
    if (rc != SQLITE_DONE) return jerr(r, 409, "That email is already used by another account.");
    Buf b = {0}; json_me(&b, uid); jsend(r, &b, NULL); free(b.p);
}
static void r_password(Req *r, int uid) {
    char old[160], nw[160]; form_get(r->body, "old", old, sizeof old); form_get(r->body, "new", nw, sizeof nw);
    if (!check_pw(uid, old)) return jerr(r, 401, "Current password is incorrect.");
    if (strlen(nw) < 8 || strlen(nw) > 100) return jerr(r, 400, "New password must be 8-100 characters.");
    char salt[33], hash[65], tok[70]; make_salt(salt); hash_pw(nw, salt, hash);
    sqlite3_stmt *s = Q("UPDATE users SET salt=?,hash=? WHERE id=?");
    sqlite3_bind_text(s, 1, salt, -1, SQLITE_STATIC); sqlite3_bind_text(s, 2, hash, -1, SQLITE_STATIC); sqlite3_bind_int(s, 3, uid);
    sqlite3_step(s); sqlite3_finalize(s);
    if (token_from_cookie(r, tok)) { /* sign out all other devices */
        s = Q("DELETE FROM sessions WHERE user_id=? AND token<>?"); sqlite3_bind_int(s, 1, uid); sqlite3_bind_text(s, 2, tok, -1, SQLITE_STATIC);
        sqlite3_step(s); sqlite3_finalize(s);
    }
    jok(r, NULL);
}
static void r_settings(Req *r, int uid) {
    char theme[16]; form_get(r->body, "theme", theme, sizeof theme);
    if (strcmp(theme, "dark") && strcmp(theme, "light")) strcpy(theme, "dark");
    int algo = form_int(r->body, "algo", 0), aging = form_int(r->body, "aging", 1), iv = form_int(r->body, "aging_interval", 3);
    int st = form_int(r->body, "aging_step", 1), tk = form_int(r->body, "tick_ms", 100), rm = form_int(r->body, "real_mode", 0);
    if (algo < 0 || algo > 1 || iv < 1 || iv > 50 || st < 1 || st > 20 || tk < 20 || tk > 500) return jerr(r, 400, "One of the settings is out of range.");
    sqlite3_stmt *s = Q("UPDATE settings SET theme=?,algo=?,aging=?,aging_interval=?,aging_step=?,tick_ms=?,real_mode=? WHERE user_id=?");
    sqlite3_bind_text(s, 1, theme, -1, SQLITE_STATIC); sqlite3_bind_int(s, 2, algo); sqlite3_bind_int(s, 3, aging ? 1 : 0);
    sqlite3_bind_int(s, 4, iv); sqlite3_bind_int(s, 5, st); sqlite3_bind_int(s, 6, tk); sqlite3_bind_int(s, 7, rm ? 1 : 0); sqlite3_bind_int(s, 8, uid);
    sqlite3_step(s); sqlite3_finalize(s);
    Buf b = {0}; json_me(&b, uid); jsend(r, &b, NULL); free(b.p);
}
static void r_delete_account(Req *r, int uid) {
    char pw[160]; form_get(r->body, "password", pw, sizeof pw);
    if (!check_pw(uid, pw)) return jerr(r, 401, "Password is incorrect.");
    sqlite3_stmt *s = Q("DELETE FROM users WHERE id=?"); sqlite3_bind_int(s, 1, uid); sqlite3_step(s); sqlite3_finalize(s);
    jok(r, "Set-Cookie: sid=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0\r\n");
}

/* ---------------- scheduler routes ---------------- */
static void json_result(Buf *b, const Proc *P, const Result *R) {
    bputs(b, "{\"procs\":[");
    for (int i = 0; i < R->n; i++) {
        if (i) bputs(b, ",");
        bputs(b, "{\"name\":"); bstr(b, P[i].name);
        bf(b, ",\"arrival\":%d,\"burst\":%d,\"prio\":%d,\"first\":%d,\"finish\":%d,\"wait\":%d,\"tat\":%d,\"resp\":%d,\"fprio\":%d}",
           P[i].arrival, P[i].burst, P[i].prio, R->first[i], R->finish[i], R->wait[i], R->tat[i], R->resp[i], R->fprio[i]);
    }
    bputs(b, "],\"gantt\":[");
    for (int i = 0; i < R->nseg; i++) bf(b, "%s{\"p\":%d,\"s\":%d,\"e\":%d}", i ? "," : "", R->seg[i].p, R->seg[i].s, R->seg[i].e);
    bf(b, "],\"avg_wait\":%.2f,\"avg_tat\":%.2f,\"avg_resp\":%.2f,\"util\":%.2f,\"throughput\":%.4f,\"ctx\":%d,\"total\":%d,\"max_wait\":%d}",
       R->avg_wait, R->avg_tat, R->avg_resp, R->util, R->throughput, R->ctx, R->total, R->maxwait);
}
static int parse_procs(const char *txt, Proc *P, char *err, size_t es) {
    int n = 0, total = 0; char *copy = strdup(txt), *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        trim(line); if (!*line) continue;
        if (n >= MAXP) { snprintf(err, es, "At most %d processes are allowed.", MAXP); free(copy); return -1; }
        char name[64]; int a, bu, pr;
        if (sscanf(line, "%63[^,],%d,%d,%d", name, &a, &bu, &pr) != 4) { snprintf(err, es, "Invalid process row: \"%.40s\"", line); free(copy); return -1; }
        trim(name);
        if (!*name || strlen(name) > 20 || strchr(name, '<') || strchr(name, '>') || strchr(name, '"')) { snprintf(err, es, "Invalid process name."); free(copy); return -1; }
        if (a < 0 || a > 200 || bu < 1 || bu > 100 || pr < 0 || pr > 99) { snprintf(err, es, "%s: arrival 0-200, burst 1-100, priority 0-99.", name); free(copy); return -1; }
        strcpy(P[n].name, name); P[n].arrival = a; P[n].burst = bu; P[n].prio = pr; total += bu; n++;
    }
    free(copy);
    if (n < 1) { snprintf(err, es, "Add at least one process."); return -1; }
    if (total > 600) { snprintf(err, es, "Total burst time is limited to 600 units."); return -1; }
    return n;
}
static void r_simulate(Req *r, int uid) {
    char procs[4096], title[128], err[160];
    form_get(r->body, "procs", procs, sizeof procs); form_get(r->body, "title", title, sizeof title); trim(title);
    Proc P[MAXP]; int n = parse_procs(procs, P, err, sizeof err); if (n < 0) return jerr(r, 400, err);
    int algo = form_int(r->body, "algo", 0) ? 1 : 0, aging = form_int(r->body, "aging", 1) ? 1 : 0;
    int iv = form_int(r->body, "aging_interval", 3), st = form_int(r->body, "aging_step", 1);
    int real = form_int(r->body, "real_mode", 0), tk = form_int(r->body, "tick_ms", 100), save = form_int(r->body, "save", 0);
    if (iv < 1 || iv > 50 || st < 1 || st > 20) return jerr(r, 400, "Aging interval must be 1-50 and step 1-20.");
    if (tk < 20 || tk > 500) return jerr(r, 400, "Tick length must be 20-500 ms.");
    Result *A = calloc(1, sizeof *A), *B = calloc(1, sizeof *B);
    Opt oa = {algo, 1, iv, st}, ob = {algo, 0, iv, st};
    run_schedule(P, n, &oa, A, NULL, NULL); run_schedule(P, n, &ob, B, NULL, NULL);
    Result *sel = aging ? A : B;
    Buf res = {0};
    bf(&res, "{\"opts\":{\"algo\":%d,\"aging\":%d,\"interval\":%d,\"step\":%d,\"tick_ms\":%d,\"real\":%d},\"aging\":", algo, aging, iv, st, tk, real);
    json_result(&res, P, A); bputs(&res, ",\"noaging\":"); json_result(&res, P, B);
    bputs(&res, ",\"real\":");
    if (real) {
        if (sel->total > 150 || (double)sel->total * tk > 20000) { free(A); free(B); free(res.p); return jerr(r, 400, "Real-process mode is limited to 150 ticks and ~20 s total. Shorten the bursts or the tick length."); }
        if (__sync_add_and_fetch(&real_runs, 1) > 2) { __sync_sub_and_fetch(&real_runs, 1); free(A); free(B); free(res.p); return jerr(r, 503, "The server is busy with other real-process runs. Try again shortly."); }
        pthread_mutex_unlock(&M);                                       /* don't block other users while real processes run */
        Real *RL_ = calloc(1, sizeof *RL_); Result *C = calloc(1, sizeof *C);
        int ok = real_start(RL_, n, tk) == 0;
        if (ok) run_schedule(P, n, aging ? &oa : &ob, C, real_tick, RL_);
        real_stop(RL_);
        pthread_mutex_lock(&M); __sync_sub_and_fetch(&real_runs, 1);
        if (ok) {
            bf(&res, "{\"elapsed_ms\":%.0f,\"nice_ok\":%d,\"nice_fail\":%d,\"procs\":[", RL_->elapsed_ms, RL_->ok, RL_->fail);
            for (int i = 0; i < n; i++) bf(&res, "%s{\"pid\":%d,\"beats\":%d}", i ? "," : "", (int)(RL_->pid[i] < 0 ? -RL_->pid[i] : RL_->pid[i]), RL_->beats[i]);
            bputs(&res, "]}");
        } else bputs(&res, "null");
        free(RL_); free(C);
    } else bputs(&res, "null");
    bputs(&res, ",\"input\":"); bstr(&res, procs); bputs(&res, "}");

    long long saved = 0;
    if (save) {
        if (!title[0]) snprintf(title, sizeof title, "Run %s", "(untitled)");
        sqlite3_stmt *s = Q("INSERT INTO runs(user_id,title,algo,aging,nproc,avg_wait,max_wait_aging,max_wait_noaging,created,result) VALUES(?,?,?,?,?,?,?,?,?,?)");
        sqlite3_bind_int(s, 1, uid); sqlite3_bind_text(s, 2, title, -1, SQLITE_STATIC); sqlite3_bind_int(s, 3, algo); sqlite3_bind_int(s, 4, aging);
        sqlite3_bind_int(s, 5, n); sqlite3_bind_double(s, 6, sel->avg_wait); sqlite3_bind_int(s, 7, A->maxwait); sqlite3_bind_int(s, 8, B->maxwait);
        sqlite3_bind_int64(s, 9, time(0)); sqlite3_bind_text(s, 10, res.p, (int)res.n, SQLITE_STATIC);
        if (sqlite3_step(s) == SQLITE_DONE) saved = sqlite3_last_insert_rowid(DB);
        sqlite3_finalize(s);
    }
    Buf out = {0}; bf(&out, "{\"ok\":true,\"saved_id\":%lld,\"result\":", saved); bcat(&out, res.p, res.n); bputs(&out, "}");
    jsend(r, &out, NULL); free(out.p); free(res.p); free(A); free(B);
}
static void r_runs(Req *r, int uid) {
    sqlite3_stmt *s = Q("SELECT id,title,algo,aging,nproc,avg_wait,max_wait_aging,max_wait_noaging,created FROM runs WHERE user_id=? ORDER BY id DESC LIMIT 100");
    sqlite3_bind_int(s, 1, uid); Buf b = {0}; bputs(&b, "{\"ok\":true,\"runs\":[");
    int first = 1;
    while (sqlite3_step(s) == SQLITE_ROW) {
        bf(&b, "%s{\"id\":%d,\"title\":", first ? "" : ",", sqlite3_column_int(s, 0)); first = 0; bstr(&b, T(s, 1));
        bf(&b, ",\"algo\":%d,\"aging\":%d,\"nproc\":%d,\"avg_wait\":%.2f,\"mw_aging\":%d,\"mw_noaging\":%d,\"created\":%lld}",
           sqlite3_column_int(s, 2), sqlite3_column_int(s, 3), sqlite3_column_int(s, 4), sqlite3_column_double(s, 5),
           sqlite3_column_int(s, 6), sqlite3_column_int(s, 7), (long long)sqlite3_column_int64(s, 8));
    }
    sqlite3_finalize(s); bputs(&b, "]}"); jsend(r, &b, NULL); free(b.p);
}
static void r_run_get(Req *r, int uid) {
    int id = atoi(strstr(r->query, "id=") ? strstr(r->query, "id=") + 3 : "0");
    sqlite3_stmt *s = Q("SELECT title,created,result FROM runs WHERE id=? AND user_id=?"); sqlite3_bind_int(s, 1, id); sqlite3_bind_int(s, 2, uid);
    if (sqlite3_step(s) != SQLITE_ROW) { sqlite3_finalize(s); return jerr(r, 404, "Run not found."); }
    Buf b = {0}; bputs(&b, "{\"ok\":true,\"title\":"); bstr(&b, T(s, 0)); bf(&b, ",\"created\":%lld,\"result\":", (long long)sqlite3_column_int64(s, 1));
    bputs(&b, T(s, 2)); bputs(&b, "}"); sqlite3_finalize(s); jsend(r, &b, NULL); free(b.p);
}
static void r_run_delete(Req *r, int uid) {
    sqlite3_stmt *s = Q("DELETE FROM runs WHERE id=? AND user_id=?"); sqlite3_bind_int(s, 1, form_int(r->body, "id", 0)); sqlite3_bind_int(s, 2, uid);
    sqlite3_step(s); sqlite3_finalize(s); jok(r, NULL);
}
static void r_stats(Req *r, int uid) {
    sqlite3_stmt *s = Q("SELECT COUNT(*),COALESCE(AVG(avg_wait),0),COALESCE(AVG(max_wait_noaging),0),COALESCE(AVG(max_wait_aging),0),COALESCE(SUM(nproc),0) FROM runs WHERE user_id=?");
    sqlite3_bind_int(s, 1, uid); Buf b = {0};
    if (sqlite3_step(s) == SQLITE_ROW)
        bf(&b, "{\"ok\":true,\"runs\":%d,\"avg_wait\":%.2f,\"mw_noaging\":%.2f,\"mw_aging\":%.2f,\"procs\":%d}",
           sqlite3_column_int(s, 0), sqlite3_column_double(s, 1), sqlite3_column_double(s, 2), sqlite3_column_double(s, 3), sqlite3_column_int(s, 4));
    sqlite3_finalize(s); jsend(r, &b, NULL); free(b.p);
}

/* ---------------- static files ---------------- */
static const char *mime(const char *p) {
    const char *e = strrchr(p, '.'); if (!e) return "application/octet-stream";
    if (!strcmp(e, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(e, ".css")) return "text/css; charset=utf-8";
    if (!strcmp(e, ".js")) return "application/javascript; charset=utf-8";
    if (!strcmp(e, ".svg")) return "image/svg+xml";
    if (!strcmp(e, ".png")) return "image/png";
    if (!strcmp(e, ".ico")) return "image/x-icon";
    return "application/octet-stream";
}
static void serve_static(Req *r) {
    char fp[700]; const char *p = r->path;
    if (strstr(p, "..") || p[0] != '/') return jerr(r, 404, "Not found");
    if (!strcmp(p, "/")) p = "/index.html";
    snprintf(fp, sizeof fp, "www%s", p);
    FILE *f = fopen(fp, "rb"); if (!f) { f = fopen("www/index.html", "rb"); p = "/index.html"; if (!f || strchr(r->path + 1, '.')) { if (f) fclose(f); return jerr(r, 404, "Not found"); } }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 8 * 1024 * 1024) { fclose(f); return jerr(r, 404, "Not found"); }
    char *d = malloc(n + 1); size_t k = fread(d, 1, n, f); fclose(f);
    respond(r, 200, mime(p), NULL, d, k); free(d);
}

/* ---------------- routing / connection handling ---------------- */
static void route(Req *r) {
    int post = !strcmp(r->method, "POST"), get = !strcmp(r->method, "GET");
    if (strncmp(r->path, "/api/", 5)) { if (get) serve_static(r); else jerr(r, 404, "Not found"); return; }
    if (post && !r->xhr) return jerr(r, 400, "Missing request header.");
    const char *a = r->path + 5;
    if (post && !strcmp(a, "signup")) return r_signup(r);
    if (post && !strcmp(a, "login")) return r_login(r);
    if (post && !strcmp(a, "logout")) return r_logout(r);
    int uid = cur_user(r);
    if (get && !strcmp(a, "me")) { if (!uid) return jerr(r, 401, "Not signed in."); Buf b = {0}; json_me(&b, uid); jsend(r, &b, NULL); free(b.p); return; }
    if (!uid) return jerr(r, 401, "Please sign in.");
    if (post && !strcmp(a, "profile")) return r_profile(r, uid);
    if (post && !strcmp(a, "password")) return r_password(r, uid);
    if (post && !strcmp(a, "settings")) return r_settings(r, uid);
    if (post && !strcmp(a, "account/delete")) return r_delete_account(r, uid);
    if (post && !strcmp(a, "simulate")) return r_simulate(r, uid);
    if (post && !strcmp(a, "runs/delete")) return r_run_delete(r, uid);
    if (get && !strcmp(a, "runs")) return r_runs(r, uid);
    if (get && !strcmp(a, "runs/get")) return r_run_get(r, uid);
    if (get && !strcmp(a, "stats")) return r_stats(r, uid);
    jerr(r, 404, "Unknown API route.");
}
static void *conn_thread(void *arg) {
    int fd = (int)(long)arg; struct timeval tv = {10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char *buf = malloc(MAX_BODY + 8192 + 1); size_t len = 0; char *hend = NULL;
    while (!hend && len < 8192) {
        ssize_t k = recv(fd, buf + len, 8192 - len, 0); if (k <= 0) goto done;
        len += k; buf[len] = 0; hend = strstr(buf, "\r\n\r\n");
    }
    if (!hend) goto done;
    Req r; memset(&r, 0, sizeof r); r.fd = fd; *hend = 0;
    char url[1100]; if (sscanf(buf, "%7s %1099s", r.method, url) != 2) goto done;
    char *q = strchr(url, '?'); if (q) { *q = 0; snprintf(r.query, sizeof r.query, "%s", q + 1); }
    snprintf(r.path, sizeof r.path, "%s", url);
    size_t clen = 0;
    for (char *l = strstr(buf, "\r\n"); l; l = strstr(l + 2, "\r\n")) {
        char *h = l + 2;
        if (!strncasecmp(h, "Content-Length:", 15)) clen = strtoul(h + 15, 0, 10);
        else if (!strncasecmp(h, "Cookie:", 7)) { h += 7; while (*h == ' ') h++; size_t m = strcspn(h, "\r\n"); if (m >= sizeof r.cookie) m = sizeof r.cookie - 1; memcpy(r.cookie, h, m); r.cookie[m] = 0; }
        else if (!strncasecmp(h, "X-Requested-With:", 17)) r.xhr = 1;
    }
    if (clen > MAX_BODY) { jerr(&r, 400, "Request too large."); goto done; }
    size_t have = len - (size_t)(hend + 4 - buf); char *body = hend + 4;
    while (have < clen) { ssize_t k = recv(fd, body + have, clen - have, 0); if (k <= 0) goto done; have += k; }
    body[clen] = 0; r.body = body; r.blen = clen;
    pthread_mutex_lock(&M); route(&r); pthread_mutex_unlock(&M);
done:
    free(buf); close(fd); return NULL;
}

/* ---------------- startup ---------------- */
static const char *SCHEMA =
 "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;"
 "CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, name TEXT NOT NULL, email TEXT UNIQUE NOT NULL, salt TEXT NOT NULL, hash TEXT NOT NULL, bio TEXT NOT NULL DEFAULT '', created INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS settings(user_id INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE, theme TEXT NOT NULL DEFAULT 'dark', algo INTEGER NOT NULL DEFAULT 0, aging INTEGER NOT NULL DEFAULT 1, aging_interval INTEGER NOT NULL DEFAULT 3, aging_step INTEGER NOT NULL DEFAULT 1, tick_ms INTEGER NOT NULL DEFAULT 100, real_mode INTEGER NOT NULL DEFAULT 0);"
 "CREATE TABLE IF NOT EXISTS sessions(token TEXT PRIMARY KEY, user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE, expires INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS runs(id INTEGER PRIMARY KEY, user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE, title TEXT NOT NULL, algo INTEGER, aging INTEGER, nproc INTEGER, avg_wait REAL, max_wait_aging INTEGER, max_wait_noaging INTEGER, created INTEGER NOT NULL, result TEXT NOT NULL);"
 "CREATE INDEX IF NOT EXISTS runs_user ON runs(user_id, id);";

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    const char *dbp = getenv("OSSP_DB"); if (!dbp) dbp = "data/ossp.db";
    if (sqlite3_open(dbp, &DB) != SQLITE_OK) { fprintf(stderr, "Cannot open database %s: %s\n(Run from the project folder: ./run.sh)\n", dbp, sqlite3_errmsg(DB)); return 1; }
    sqlite3_busy_timeout(DB, 3000);
    char *em = NULL; if (sqlite3_exec(DB, SCHEMA, 0, 0, &em) != SQLITE_OK) { fprintf(stderr, "Schema error: %s\n", em); return 1; }
    int port = getenv("PORT") ? atoi(getenv("PORT")) : 8080, ls = socket(AF_INET, SOCK_STREAM, 0), one = 1, bound = 0;
    fcntl(ls, F_SETFD, FD_CLOEXEC);
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa = {0}; sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(getenv("OSSP_PUBLIC") ? INADDR_ANY : INADDR_LOOPBACK);
    for (int i = 0; i < 20 && !bound; i++) { sa.sin_port = htons(port + i); if (bind(ls, (struct sockaddr *)&sa, sizeof sa) == 0) { port += i; bound = 1; } }
    if (!bound || listen(ls, 64) < 0) { fprintf(stderr, "Could not bind to a port near %d.\n", port); return 1; }
    printf("\n  Priority Scheduler web app is running\n  Open:  http://localhost:%d\n  Stop:  Ctrl+C\n\n", port); fflush(stdout);
    if (!getenv("NO_BROWSER")) {
        char cmd[400];
        snprintf(cmd, sizeof cmd, "(xdg-open http://localhost:%d || wslview http://localhost:%d || cmd.exe /c start http://localhost:%d || open http://localhost:%d) >/dev/null 2>&1 &", port, port, port, port);
        if (system(cmd)) {}
    }
    for (;;) {
        int c = accept(ls, NULL, NULL); if (c >= 0) fcntl(c, F_SETFD, FD_CLOEXEC); if (c < 0) { if (errno == EINTR) continue; usleep(10000); continue; }
        pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&at, 1 << 20);
        if (pthread_create(&t, &at, conn_thread, (void *)(long)c) != 0) close(c);
        pthread_attr_destroy(&at);
    }
}
