#define _GNU_SOURCE
#include "pscheduler.h"
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/resource.h>

static void add_seg(Result *R, int p, int s, int e) {
    if (R->nseg && R->seg[R->nseg-1].p == p && R->seg[R->nseg-1].e == s) { R->seg[R->nseg-1].e = e; return; }
    if (R->nseg < MAXSEG) { R->seg[R->nseg].p = p; R->seg[R->nseg].s = s; R->seg[R->nseg].e = e; R->nseg++; }
}

/* Tick-based priority scheduler. Lower number = higher priority (like Linux nice).
   Aging: a ready process that waits `interval` ticks gets its number reduced by `step`. */
int run_schedule(const Proc *P, int n, const Opt *o, Result *R, tick_cb cb, void *ud) {
    int rem[MAXP], pr[MAXP], wc[MAXP], done = 0, t = INT_MAX, cur = -1, last = -1;
    memset(R, 0, sizeof *R); R->n = n;
    for (int i = 0; i < n; i++) {
        rem[i] = P[i].burst; pr[i] = P[i].prio; wc[i] = 0; R->first[i] = -1;
        if (P[i].arrival < t) t = P[i].arrival;
    }
    int t0 = t;
    while (done < n) {
        int c = -1;
        if (cur >= 0 && o->algo == 1) c = cur;
        else for (int i = 0; i < n; i++)
            if (rem[i] > 0 && P[i].arrival <= t &&
                (c < 0 || pr[i] < pr[c] || (pr[i] == pr[c] && P[i].arrival < P[c].arrival))) c = i;
        if (c < 0) { if (cb) cb(ud, -1, 0); add_seg(R, -1, t, t + 1); t++; cur = -1; continue; }
        if (last >= 0 && last != c) R->ctx++;
        last = c;
        if (R->first[c] < 0) R->first[c] = t;
        if (cb) cb(ud, c, pr[c]);
        add_seg(R, c, t, t + 1); R->busy++; rem[c]--; wc[c] = 0;
        for (int i = 0; i < n; i++)
            if (i != c && rem[i] > 0 && P[i].arrival <= t && o->aging && ++wc[i] >= o->interval) {
                wc[i] = 0; pr[i] -= o->step; if (pr[i] < 0) pr[i] = 0;
            }
        t++;
        if (rem[c] == 0) { R->finish[c] = t; done++; cur = -1; } else cur = c;
    }
    R->total = t - t0;
    double sw = 0, st = 0, sr = 0;
    for (int i = 0; i < n; i++) {
        R->tat[i] = R->finish[i] - P[i].arrival; R->wait[i] = R->tat[i] - P[i].burst;
        R->resp[i] = R->first[i] - P[i].arrival; R->fprio[i] = pr[i];
        sw += R->wait[i]; st += R->tat[i]; sr += R->resp[i];
        if (R->wait[i] > R->maxwait) R->maxwait = R->wait[i];
    }
    R->avg_wait = sw / n; R->avg_tat = st / n; R->avg_resp = sr / n;
    R->util = R->total ? 100.0 * R->busy / R->total : 0;
    R->throughput = R->total ? (double)n / R->total : 0;
    return 0;
}

/* ---------- real-process mode ---------- */
static void drain(Real *R, int i) {
    char b[512]; ssize_t k;
    while ((k = read(R->fd[i], b, sizeof b)) > 0) for (ssize_t j = 0; j < k; j++) if (b[j] == '\n') R->beats[i]++;
}
int real_start(Real *R, int n, int tick_ms) {
    memset(R, 0, sizeof *R); R->n = n; R->tick_ms = tick_ms;
    for (int i = 0; i < n; i++) { R->pid[i] = -1; R->fd[i] = -1; }
    for (int i = 0; i < n; i++) {
        int p[2];
        if (pipe(p) < 0) { real_stop(R); return -1; }
        fcntl(p[0], F_SETFD, FD_CLOEXEC); fcntl(p[1], F_SETFD, FD_CLOEXEC);
        pid_t c = fork();
        if (c < 0) { close(p[0]); close(p[1]); real_stop(R); return -1; }
        if (c == 0) { /* child: only async-signal-safe calls until exec */
            setpgid(0, 0);
            int dn = open("/dev/null", O_RDWR);
            if (dn >= 0) { dup2(dn, 0); dup2(dn, 2); }
            dup2(p[1], 1);
            kill(getpid(), SIGSTOP);              /* wait until the scheduler dispatches us */
            execl("/bin/sh", "sh", "-c", "while :; do echo t; sleep 0.01; done", (char *)NULL);
            _exit(127);
        }
        close(p[1]); setpgid(c, c);
        int st; waitpid(c, &st, WUNTRACED);
        fcntl(p[0], F_SETFL, O_NONBLOCK);
        R->pid[i] = c; R->fd[i] = p[0]; R->nice[i] = 0;
    }
    return 0;
}
void real_tick(void *ud, int idx, int prio) {
    Real *R = ud; struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    if (idx >= 0) {
        int nv = prio * 2; if (nv > 19) nv = 19;
        if (nv != R->nice[idx]) { if (setpriority(PRIO_PROCESS, R->pid[idx], nv) == 0) { R->ok++; R->nice[idx] = nv; } else R->fail++; }
        kill(-R->pid[idx], SIGCONT);
    }
    usleep((useconds_t)R->tick_ms * 1000);
    if (idx >= 0) { kill(-R->pid[idx], SIGSTOP); drain(R, idx); }
    clock_gettime(CLOCK_MONOTONIC, &b);
    R->elapsed_ms += (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
}
void real_stop(Real *R) {
    for (int i = 0; i < R->n; i++) {
        if (R->pid[i] > 0) { kill(-R->pid[i], SIGKILL); int st; waitpid(R->pid[i], &st, 0); R->pid[i] = -R->pid[i]; }
        if (R->fd[i] >= 0) { close(R->fd[i]); R->fd[i] = -1; }
    }
}
