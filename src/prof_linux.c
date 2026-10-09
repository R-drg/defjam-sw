/*
 * prof_linux.c -- a sampling profiler for Linux bring-up and performance.
 *
 * DEFJAM_PROF=<path>[,<hz>[,<delay s>]] samples the running threads with
 * ITIMER_PROF (CPU time, default 500 Hz; from <delay> seconds on, default
 * 10) and writes, per sample, the thread id and the host return addresses
 * of its stack (backtrace). tools/prof_linux_report.py
 * which name guest code directly.
 *
 * Unprivileged perf is off on this host (perf_event_paranoid=4), and gdb
 * snapshots only show where threads sleep, not what they spend CPU on.
 */
#if defined(__linux__) && !defined(__SWITCH__)
#define _GNU_SOURCE
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

#define PROF_DEPTH 24
/* Samples kept (~40 MB). Allocated by the start thread after the delay: a
 * big allocation before xbox_MemoryLayoutInit took address space the guest
 * layout maps later, and the boot crashed in the contiguous window. */
#define PROF_MAX   (1u << 18)

typedef struct {
    uint32_t tid;
    uint32_t n;
    void *pc[PROF_DEPTH];
} ProfSample;

static ProfSample *s_buf;
static volatile uint32_t s_count;
static char s_path[512];

static void prof_signal(int sig)
{
    uint32_t i;
    (void)sig;
    i = __atomic_fetch_add(&s_count, 1, __ATOMIC_RELAXED);
    if (i >= PROF_MAX)
        return;
    s_buf[i].tid = (uint32_t)syscall(SYS_gettid);
    s_buf[i].n = (uint32_t)backtrace(s_buf[i].pc, PROF_DEPTH);
}

static void prof_write(void)
{
    uint32_t i, n = s_count < PROF_MAX ? s_count : PROF_MAX;
    FILE *f;
    struct itimerval off;

    memset(&off, 0, sizeof(off));
    setitimer(ITIMER_PROF, &off, NULL);
    if (!s_buf)
        return;
    f = fopen(s_path, "w");
    if (!f)
        return;
    /* Load base, so addresses can be symbolised against the ELF. */
    fprintf(f, "base %p\n", (void *)&prof_write);
    for (i = 0; i < n; i++) {
        uint32_t k;
        fprintf(f, "%u", s_buf[i].tid);
        for (k = 0; k < s_buf[i].n; k++)
            fprintf(f, " %p", s_buf[i].pc[k]);
        fputc('\n', f);
    }
    fclose(f);
    fprintf(stderr, "[prof] %u samples -> %s\n", n, s_path);
}

static void prof_exit_signal(int sig)
{
    prof_write();
    signal(sig, SIG_DFL);
    raise(sig);
}

static int s_hz = 500, s_delay = 10;

static void *prof_start(void *arg)
{
    struct itimerval it;
    (void)arg;
    sleep((unsigned)s_delay);
    s_buf = calloc(PROF_MAX, sizeof(*s_buf));
    if (!s_buf)
        return NULL;
    signal(SIGPROF, prof_signal);
    memset(&it, 0, sizeof(it));
    it.it_interval.tv_usec = 1000000 / s_hz;
    it.it_value = it.it_interval;
    setitimer(ITIMER_PROF, &it, NULL);
    fprintf(stderr, "[prof] sampling started\n");
    return NULL;
}

void defjam_prof_init(void)
{
    const char *e = getenv("DEFJAM_PROF");
    const char *comma;
    void *warm[2];
    pthread_t th;

    if (!e || !*e)
        return;
    comma = strchr(e, ',');
    snprintf(s_path, sizeof(s_path), "%.*s",
             comma ? (int)(comma - e) : (int)strlen(e), e);
    if (comma && atoi(comma + 1) > 0)
        s_hz = atoi(comma + 1);
    if (comma && strchr(comma + 1, ','))
        s_delay = atoi(strchr(comma + 1, ',') + 1);
    backtrace(warm, 2);                /* loads libgcc before any signal */
    /* SIGUSR2 or exit: write the samples out (tools stop runs with kill). */
    signal(SIGUSR2, prof_exit_signal);
    signal(SIGTERM, prof_exit_signal);
    atexit(prof_write);
    pthread_create(&th, NULL, prof_start, NULL);
    pthread_detach(th);
    fprintf(stderr, "[prof] sampling at %d Hz after %d s -> %s (SIGUSR2 writes)\n",
            s_hz, s_delay, s_path);
}
#else
void defjam_prof_init(void) {}
#endif
