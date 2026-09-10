/*
 * 2026 - Team Resurgent
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Part of RXDK - see LICENSE.md for the full GNU GPL v3.
 */

/*
 * Linkable stubs for POSIX facilities a single-title console fundamentally
 * cannot provide: multi-process spawning, pipes/FIFOs, pseudo-terminals,
 * inter-process IPC (named semaphores, message queues, shared memory, System V
 * IPC), per-process interval timers, scheduling policy, user/group identity,
 * runtime dynamic loading (dlopen), password hashing (crypt) and shell word
 * expansion (wordexp). They exist only so portable code links; each fails
 * cleanly (ENOSYS/-1) or returns the single-title console's fixed identity.
 *
 * Every stub is tagged  RXDK-STUB  so the whole set is greppable when any of
 * these facilities later becomes implementable (e.g. sockets/pipes over XNet).
 */
#define _GNU_SOURCE 1
#define _POSIX_PRIORITY_SCHEDULING 1   /* expose <sched.h> policy declarations */

#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sched.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <semaphore.h>
#include <mqueue.h>
#include <iconv.h>
#include <dlfcn.h>
#include <wordexp.h>
#include <crypt.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>

/* ---- multi-process spawning (no process model) ---- */
int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *fa,
                const posix_spawnattr_t *attr,
                char *const argv[], char *const envp[]) {
    (void)pid; (void)path; (void)fa; (void)attr; (void)argv; (void)envp;
    return ENOSYS;   /* RXDK-STUB: no process spawning */
}
int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *fa,
                 const posix_spawnattr_t *attr,
                 char *const argv[], char *const envp[]) {
    (void)pid; (void)file; (void)fa; (void)attr; (void)argv; (void)envp;
    return ENOSYS;   /* RXDK-STUB */
}
pid_t vfork(void) { errno = ENOSYS; return -1; }   /* RXDK-STUB: like fork */

/* ---- pipes / FIFOs / device nodes (no such kernel objects) ---- */
int pipe(int fds[2])              { (void)fds; errno = ENOSYS; return -1; } /* RXDK-STUB */
int pipe2(int fds[2], int flags)  { (void)fds; (void)flags; errno = ENOSYS; return -1; } /* RXDK-STUB */
int mkfifo(const char *p, mode_t m)            { (void)p; (void)m; errno = ENOSYS; return -1; } /* RXDK-STUB */
int mknod(const char *p, mode_t m, dev_t dev)  { (void)p; (void)m; (void)dev; errno = ENOSYS; return -1; } /* RXDK-STUB */

/* ---- pseudo-terminals (the console has no ptys) ---- */
int   grantpt(int fd)     { (void)fd; errno = ENOSYS; return -1; } /* RXDK-STUB */
int   unlockpt(int fd)    { (void)fd; errno = ENOSYS; return -1; } /* RXDK-STUB */
char *ptsname(int fd)     { (void)fd; errno = ENOSYS; return NULL; } /* RXDK-STUB */
int   posix_openpt(int f) { (void)f;  errno = ENOSYS; return -1; } /* RXDK-STUB */

/* ---- extended signals (a title receives none) ---- */
int sigsuspend(const sigset_t *m)  { (void)m; errno = ENOSYS; return -1; } /* RXDK-STUB */
int sigpending(sigset_t *set)      { if (set) *set = 0; return 0; }        /* RXDK-STUB: nothing pending */
int sigwait(const sigset_t *s, int *sig) { (void)s; (void)sig; return ENOSYS; } /* RXDK-STUB */
int sigqueue(pid_t pid, int sig, const union sigval v) { (void)pid; (void)sig; (void)v; errno = EPERM; return -1; } /* RXDK-STUB */
int killpg(pid_t pgrp, int sig)    { (void)pgrp; (void)sig; errno = EPERM; return -1; } /* RXDK-STUB */

/* ---- interval alarm: no SIGALRM delivery. timer_create (SIGEV_THREAD/NONE) is
   real -- see timer.c. ---- */
useconds_t ualarm(useconds_t u, useconds_t i) { (void)u; (void)i; return 0; } /* RXDK-STUB: no SIGALRM */

/* ---- process groups / sessions (one title, one group) ---- */
int   setpgid(pid_t pid, pid_t pgid)  { (void)pid; (void)pgid; return 0; } /* RXDK-STUB */
pid_t getpgrp(void)                   { return 1; }  /* RXDK-STUB */
pid_t getpgid(pid_t pid)              { (void)pid; return 1; } /* RXDK-STUB */
pid_t setsid(void)                    { return 1; }  /* RXDK-STUB */
pid_t getsid(pid_t pid)               { (void)pid; return 1; } /* RXDK-STUB */
pid_t tcgetpgrp(int fd)               { (void)fd; return 1; }  /* RXDK-STUB */
int   tcsetpgrp(int fd, pid_t pgrp)   { (void)fd; (void)pgrp; return 0; } /* RXDK-STUB */

/* ---- scheduling policy / priority (one scheduling class) ---- */
int sched_get_priority_max(int policy) { (void)policy; return 0; } /* RXDK-STUB */
int sched_get_priority_min(int policy) { (void)policy; return 0; } /* RXDK-STUB */
int sched_getscheduler(pid_t pid)      { (void)pid; return 0; /*SCHED_OTHER*/ } /* RXDK-STUB */
int sched_setscheduler(pid_t pid, int policy, const struct sched_param *p) { (void)pid; (void)policy; (void)p; return 0; } /* RXDK-STUB */
int sched_getparam(pid_t pid, struct sched_param *p) { (void)pid; if (p) p->sched_priority = 0; return 0; } /* RXDK-STUB */
int sched_setparam(pid_t pid, const struct sched_param *p) { (void)pid; (void)p; return 0; } /* RXDK-STUB */
int nice(int inc)                      { (void)inc; return 0; } /* RXDK-STUB */
int getpriority(int which, id_t who)   { (void)which; (void)who; return 0; } /* RXDK-STUB */
int setpriority(int which, id_t who, int prio) { (void)which; (void)who; (void)prio; return 0; } /* RXDK-STUB */

/* ---- user / group identity (a title runs as a single fixed identity) ---- */
int setuid(uid_t u)  { (void)u; return 0; } /* RXDK-STUB */
int setgid(gid_t g)  { (void)g; return 0; } /* RXDK-STUB */
int seteuid(uid_t u) { (void)u; return 0; } /* RXDK-STUB */
int setegid(gid_t g) { (void)g; return 0; } /* RXDK-STUB */
int setreuid(uid_t r, uid_t e) { (void)r; (void)e; return 0; } /* RXDK-STUB */
int setregid(gid_t r, gid_t e) { (void)r; (void)e; return 0; } /* RXDK-STUB */

/* ---- resource usage / accounting (none tracked) ---- */
int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (usage) { char *p = (char *)usage; size_t i; for (i = 0; i < sizeof *usage; i++) p[i] = 0; }
    return 0;   /* RXDK-STUB: zeroed usage */
}
pid_t wait3(int *status, int options, struct rusage *usage) { (void)status; (void)options; (void)usage; errno = ECHILD; return -1; } /* RXDK-STUB */
pid_t wait4(pid_t pid, int *status, int options, struct rusage *usage) { (void)pid; (void)status; (void)options; (void)usage; errno = ECHILD; return -1; } /* RXDK-STUB */

/* ---- virtual-memory management (title memory is already resident/RWX) ---- */
int msync(void *a, size_t l, int f)   { (void)a; (void)l; (void)f; return 0; } /* RXDK-STUB */
int madvise(void *a, size_t l, int adv)        { (void)a; (void)l; (void)adv; return 0; } /* RXDK-STUB */
int posix_madvise(void *a, size_t l, int adv)  { (void)a; (void)l; (void)adv; return 0; } /* RXDK-STUB */
int mlock(const void *a, size_t l)    { (void)a; (void)l; return 0; } /* RXDK-STUB: always resident */
int munlock(const void *a, size_t l)  { (void)a; (void)l; return 0; } /* RXDK-STUB */
int mlockall(int flags)               { (void)flags; return 0; } /* RXDK-STUB */
int munlockall(void)                  { return 0; } /* RXDK-STUB */
int syncfs(int fd)                    { (void)fd; return 0; } /* RXDK-STUB: sync() flushes all */

/* ---- shared memory (no cross-process namespace) ---- */
int shm_open(const char *name, int oflag, mode_t mode) { (void)name; (void)oflag; (void)mode; errno = ENOSYS; return -1; } /* RXDK-STUB */
int shm_unlink(const char *name)      { (void)name; errno = ENOSYS; return -1; } /* RXDK-STUB */

/* ---- named semaphores (the unnamed sem_* in sem.c are real) ---- */
sem_t *sem_open(const char *name, int oflag, ...) { (void)name; (void)oflag; errno = ENOSYS; return SEM_FAILED; } /* RXDK-STUB */
int    sem_close(sem_t *s)            { (void)s; errno = ENOSYS; return -1; } /* RXDK-STUB */
int    sem_unlink(const char *name)   { (void)name; errno = ENOSYS; return -1; } /* RXDK-STUB */

/* ---- POSIX message queues (inter-process IPC) ---- */
mqd_t   mq_open(const char *name, int oflag, ...) { (void)name; (void)oflag; errno = ENOSYS; return (mqd_t)-1; } /* RXDK-STUB */
int     mq_close(mqd_t q)             { (void)q; errno = ENOSYS; return -1; } /* RXDK-STUB */
int     mq_unlink(const char *name)   { (void)name; errno = ENOSYS; return -1; } /* RXDK-STUB */
int     mq_send(mqd_t q, const char *m, size_t l, unsigned p)   { (void)q; (void)m; (void)l; (void)p; errno = ENOSYS; return -1; } /* RXDK-STUB */
ssize_t mq_receive(mqd_t q, char *m, size_t l, unsigned *p)     { (void)q; (void)m; (void)l; (void)p; errno = ENOSYS; return -1; } /* RXDK-STUB */
int     mq_getattr(mqd_t q, struct mq_attr *a)                  { (void)q; (void)a; errno = ENOSYS; return -1; } /* RXDK-STUB */
int     mq_setattr(mqd_t q, const struct mq_attr *n, struct mq_attr *o) { (void)q; (void)n; (void)o; errno = ENOSYS; return -1; } /* RXDK-STUB */

/* ---- iconv: charset conversion is gated on picolibc's multibyte support, which
   the config deliberately disables (__MB_CAPABLE off -> wchar is narrowed by
   truncation, the behaviour Xbox paths/gamertags want -- see runtime/config/
   picolibc.h). Provided as a CLEAN-failing stub so code links and detects the
   failure; to make it real, define __MB_CAPABLE and build libc/iconv. Note we do
   NOT enable picolibc's own iconv here: with __MB_CAPABLE off its iconv_open
   returns NULL (not (iconv_t)-1) for ascii, which callers misread as success. */
iconv_t iconv_open(const char *to, const char *from) {
    (void)to; (void)from; errno = EINVAL; return (iconv_t)-1; /* RXDK-STUB */
}
size_t iconv(iconv_t cd, char **in, size_t *il, char **out, size_t *ol) {
    (void)cd; (void)in; (void)il; (void)out; (void)ol; errno = EBADF; return (size_t)-1; /* RXDK-STUB */
}
int iconv_close(iconv_t cd) { (void)cd; return 0; } /* RXDK-STUB */

/* ---- runtime dynamic loading (a title is one statically-linked image; there
   is no loader, symbols resolve at link time) ---- */
static const char *g_dlerr;
void *dlopen(const char *file, int mode) {
    (void)file; (void)mode;
    g_dlerr = "dlopen: no runtime dynamic loader on this platform"; /* RXDK-STUB */
    return NULL;
}
void *dlsym(void *handle, const char *name) {
    (void)handle; (void)name;
    g_dlerr = "dlsym: no runtime dynamic loader on this platform"; /* RXDK-STUB */
    return NULL;
}
int dlclose(void *handle) { (void)handle; return 0; } /* RXDK-STUB: nothing was opened */
char *dlerror(void) { const char *e = g_dlerr; g_dlerr = NULL; return (char *)e; } /* RXDK-STUB */

/* ---- password hashing: no DES/MD5/bcrypt back end ---- */
char *crypt(const char *key, const char *salt) {
    (void)key; (void)salt; errno = ENOSYS; return NULL; /* RXDK-STUB */
}
char *crypt_r(const char *key, const char *salt, struct crypt_data *data) {
    (void)key; (void)salt; (void)data; errno = ENOSYS; return NULL; /* RXDK-STUB */
}

/* ---- shell word expansion: no shell / command substitution ---- */
int  wordexp(const char *words, wordexp_t *pwordexp, int flags) {
    (void)words; (void)pwordexp; (void)flags; return WRDE_NOSYS; /* RXDK-STUB */
}
void wordfree(wordexp_t *pwordexp) { (void)pwordexp; } /* RXDK-STUB: nothing allocated */

/* ---- alternate signal stack: cooperative signals never switch stacks ---- */
int sigaltstack(const stack_t *ss, stack_t *oss) {
    (void)ss; (void)oss; errno = ENOSYS; return -1; /* RXDK-STUB */
}

/* ---- waitid: no child processes ---- */
int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
    (void)idtype; (void)id; (void)infop; (void)options; errno = ECHILD; return -1; /* RXDK-STUB */
}

/* ---- pthread fork handlers: a title never fork()s, so there is nothing to run
   on either side of one. Registration silently succeeds. ---- */
int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void)) {
    (void)prepare; (void)parent; (void)child; return 0; /* RXDK-STUB */
}

/* ---- System V IPC (shared memory / message queues / semaphore sets): a cross-
   process namespace the console has no concept of. The real in-process POSIX
   semaphores are sem.c / <semaphore.h>. ---- */
key_t ftok(const char *path, int id) { (void)path; (void)id; errno = ENOSYS; return (key_t)-1; } /* RXDK-STUB */

int   shmget(key_t k, size_t sz, int f)            { (void)k; (void)sz; (void)f; errno = ENOSYS; return -1; } /* RXDK-STUB */
void *shmat(int id, const void *addr, int f)       { (void)id; (void)addr; (void)f; errno = ENOSYS; return (void *)-1; } /* RXDK-STUB */
int   shmdt(const void *addr)                      { (void)addr; errno = ENOSYS; return -1; } /* RXDK-STUB */
int   shmctl(int id, int cmd, struct shmid_ds *b)  { (void)id; (void)cmd; (void)b; errno = ENOSYS; return -1; } /* RXDK-STUB */

int     msgget(key_t k, int f)                                  { (void)k; (void)f; errno = ENOSYS; return -1; } /* RXDK-STUB */
int     msgsnd(int id, const void *m, size_t sz, int f)         { (void)id; (void)m; (void)sz; (void)f; errno = ENOSYS; return -1; } /* RXDK-STUB */
ssize_t msgrcv(int id, void *m, size_t sz, long t, int f)       { (void)id; (void)m; (void)sz; (void)t; (void)f; errno = ENOSYS; return -1; } /* RXDK-STUB */
int     msgctl(int id, int cmd, struct msqid_ds *b)             { (void)id; (void)cmd; (void)b; errno = ENOSYS; return -1; } /* RXDK-STUB */

int semget(key_t k, int n, int f)            { (void)k; (void)n; (void)f; errno = ENOSYS; return -1; } /* RXDK-STUB */
int semop(int id, struct sembuf *o, size_t n){ (void)id; (void)o; (void)n; errno = ENOSYS; return -1; } /* RXDK-STUB */
int semctl(int id, int num, int cmd, ...)    { (void)id; (void)num; (void)cmd; errno = ENOSYS; return -1; } /* RXDK-STUB */
