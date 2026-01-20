/**
 * 用户态库实现
 */
#include "user.h"

#define SYS_OPEN            56
#define SYS_CLOSE           57
#define SYS_READ            63
#define SYS_WRITE           64
#define SYS_EXIT            93
#define SYS_SCHED_YIELD     124
#define SYS_CLOCK_GETTIME   113
#define SYS_GETPID          172
#define SYS_FORK            220
#define SYS_EXEC            221
#define SYS_KILL            129
#define SYS_SIGACTION       134
#define SYS_SIGPROCMASK     135
#define SYS_SIGRETURN       139
#define SYS_WAITPID         260
#define SYS_THREAD_CREATE   1000
#define SYS_GETTID          1001
#define SYS_WAITTID         1002
#define SYS_MUTEX_CREATE    1010
#define SYS_MUTEX_LOCK      1011
#define SYS_MUTEX_UNLOCK    1012
#define SYS_SEMAPHORE_CREATE 1020
#define SYS_SEMAPHORE_UP    1021
#define SYS_SEMAPHORE_DOWN  1022
#define SYS_CONDVAR_CREATE  1030
#define SYS_CONDVAR_SIGNAL  1031
#define SYS_CONDVAR_WAIT    1032

static long syscall(long n, long a0, long a1, long a2) {
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    register long _a2 asm("a2") = a2;
    register long _a7 asm("a7") = n;
    asm volatile("ecall"
                 : "+r"(_a0)
                 : "r"(_a1), "r"(_a2), "r"(_a7)
                 : "memory");
    return _a0;
}

int sys_open(const char *path, unsigned int flags) {
    return syscall(SYS_OPEN, (long)path, flags, 0);
}

int sys_close(int fd) {
    return syscall(SYS_CLOSE, fd, 0, 0);
}

int sys_read(int fd, void *buf, size_t count) {
    return syscall(SYS_READ, fd, (long)buf, count);
}

int sys_write(int fd, const void *buf, size_t count) {
    return syscall(SYS_WRITE, fd, (long)buf, count);
}

void sys_exit(int code) {
    syscall(SYS_EXIT, code, 0, 0);
    __builtin_unreachable();
}

int sys_sched_yield(void) {
    return syscall(SYS_SCHED_YIELD, 0, 0, 0);
}

int sys_clock_gettime(int clock_id, timespec_t *tp) {
    return syscall(SYS_CLOCK_GETTIME, clock_id, (long)tp, 0);
}

int sys_getpid(void) {
    return syscall(SYS_GETPID, 0, 0, 0);
}

int sys_fork(void) {
    return syscall(SYS_FORK, 0, 0, 0);
}

int sys_exec(const char *path, size_t len) {
    return syscall(SYS_EXEC, (long)path, len, 0);
}

int sys_waitpid(int pid, int *exit_code) {
    return syscall(SYS_WAITPID, pid, (long)exit_code, 0);
}

int sys_kill(int pid, int signum) {
    return syscall(SYS_KILL, pid, signum, 0);
}

int sys_sigaction(int signum, const void *action, void *old_action) {
    return syscall(SYS_SIGACTION, signum, (long)action, (long)old_action);
}

int sys_sigprocmask(unsigned long mask) {
    return syscall(SYS_SIGPROCMASK, mask, 0, 0);
}

int sys_sigreturn(void) {
    return syscall(SYS_SIGRETURN, 0, 0, 0);
}

int sys_thread_create(void (*entry)(unsigned long), unsigned long arg) {
    return syscall(SYS_THREAD_CREATE, (long)entry, arg, 0);
}

int sys_gettid(void) {
    return syscall(SYS_GETTID, 0, 0, 0);
}

int sys_waittid(int tid) {
    return syscall(SYS_WAITTID, tid, 0, 0);
}

int sys_mutex_create(void) {
    return syscall(SYS_MUTEX_CREATE, 0, 0, 0);
}

int sys_mutex_blocking_create(void) {
    return syscall(SYS_MUTEX_CREATE, 1, 0, 0);
}

int sys_mutex_lock(int mutex_id) {
    return syscall(SYS_MUTEX_LOCK, mutex_id, 0, 0);
}

int sys_mutex_unlock(int mutex_id) {
    return syscall(SYS_MUTEX_UNLOCK, mutex_id, 0, 0);
}

int sys_semaphore_create(int res_count) {
    return syscall(SYS_SEMAPHORE_CREATE, res_count, 0, 0);
}

int sys_semaphore_up(int sem_id) {
    return syscall(SYS_SEMAPHORE_UP, sem_id, 0, 0);
}

int sys_semaphore_down(int sem_id) {
    return syscall(SYS_SEMAPHORE_DOWN, sem_id, 0, 0);
}

int sys_condvar_create(void) {
    return syscall(SYS_CONDVAR_CREATE, 0, 0, 0);
}

int sys_condvar_signal(int condvar_id) {
    return syscall(SYS_CONDVAR_SIGNAL, condvar_id, 0, 0);
}

int sys_condvar_wait(int condvar_id, int mutex_id) {
    return syscall(SYS_CONDVAR_WAIT, condvar_id, mutex_id, 0);
}

void putchar(char c) {
    sys_write(STDOUT, &c, 1);
}

void puts(const char *s) {
    while (*s) putchar(*s++);
    putchar('\n');
}

void print_str(const char *s) {
    while (*s) putchar(*s++);
}

void print_int(unsigned int n) {
    char buf[12];
    int i = 0;
    if (n == 0) {
        putchar('0');
        return;
    }
    while (n) {
        buf[i++] = '0' + n % 10;
        n /= 10;
    }
    while (i--) putchar(buf[i]);
}

void print_long(unsigned long n) {
    char buf[24];
    int i = 0;
    if (n == 0) {
        putchar('0');
        return;
    }
    while (n) {
        buf[i++] = '0' + n % 10;
        n /= 10;
    }
    while (i--) putchar(buf[i]);
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}
