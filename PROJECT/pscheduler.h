#ifndef PSCHED_H
#define PSCHED_H
#include <sys/types.h>
#define MAXP 20
#define MAXSEG 1100
typedef struct { char name[24]; int arrival, burst, prio; } Proc;
typedef struct { int algo; /* 0 preemptive, 1 non-preemptive */ int aging, interval, step; } Opt;
typedef struct { int p, s, e; } Seg;
typedef struct {
    int n, nseg, total, busy, ctx, maxwait;
    int first[MAXP], finish[MAXP], wait[MAXP], tat[MAXP], resp[MAXP], fprio[MAXP];
    double avg_wait, avg_tat, avg_resp, util, throughput;
    Seg seg[MAXSEG];
} Result;
typedef void (*tick_cb)(void *ud, int idx, int prio);
int run_schedule(const Proc *P, int n, const Opt *o, Result *R, tick_cb cb, void *ud);

/* real-process executor: each process is a real child (fork+exec) controlled with SIGSTOP/SIGCONT */
typedef struct {
    int n, tick_ms, ok, fail; double elapsed_ms;
    pid_t pid[MAXP]; int fd[MAXP], beats[MAXP], nice[MAXP];
} Real;
int real_start(Real *R, int n, int tick_ms);
void real_tick(void *ud, int idx, int prio);
void real_stop(Real *R);
#endif
