/**
 * 系统调用处理框架
 */
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stddef.h>
#include <stdint.h>

/* 系统调用号 */
#define SYS_OPEN            56
#define SYS_CLOSE           57
#define SYS_READ            63
#define SYS_WRITE           64
#define SYS_EXIT            93
#define SYS_SCHED_YIELD     124
#define SYS_KILL            129
#define SYS_SIGACTION       134
#define SYS_SIGPROCMASK     135
#define SYS_SIGRETURN       139
#define SYS_CLOCK_GETTIME   113
#define SYS_GETPID          172
#define SYS_FORK            220
#define SYS_EXEC            221
#define SYS_WAITPID         260

/* 线程相关 */
#define SYS_THREAD_CREATE   1000
#define SYS_GETTID          1001
#define SYS_WAITTID         1002

/* 同步原语 */
#define SYS_MUTEX_CREATE    1010
#define SYS_MUTEX_LOCK      1011
#define SYS_MUTEX_UNLOCK    1012
#define SYS_SEMAPHORE_CREATE 1020
#define SYS_SEMAPHORE_UP    1021
#define SYS_SEMAPHORE_DOWN  1022
#define SYS_CONDVAR_CREATE  1030
#define SYS_CONDVAR_SIGNAL  1031
#define SYS_CONDVAR_WAIT    1032

/* 标准文件描述符 */
#define FD_STDIN    0
#define FD_STDOUT   1
#define FD_STDERR   2

/* 时钟类型 */
#define CLOCK_REALTIME      0
#define CLOCK_MONOTONIC     1

/* 时间结构 */
typedef struct {
    uintptr_t tv_sec;
    uintptr_t tv_nsec;
} timespec_t;

/* 系统调用结果 */
typedef enum {
    SYSCALL_OK,         /* 正常完成 */
    SYSCALL_UNSUPPORTED /* 不支持的调用 */
} syscall_ret_t;

typedef struct {
    syscall_ret_t status;
    long value;         /* OK: 返回值, UNSUPPORTED: 调用号 */
} syscall_result_t;

/**
 * IO 操作接口
 */
typedef struct {
    long (*write)(int fd, const void *buf, size_t count);
    long (*read)(int fd, void *buf, size_t count);
    long (*open)(const char *path, uint32_t flags);
    long (*close)(int fd);
} syscall_io_t;

/**
 * 进程操作接口 (ch5 扩展)
 */
typedef struct {
    void (*exit)(int code);
    long (*fork)(void);
    long (*exec)(const char *path, size_t len);
    long (*waitpid)(long pid, int *exit_code);
    long (*getpid)(void);
} syscall_proc_t;

/**
 * 调度操作接口
 */
typedef struct {
    long (*sched_yield)(void);
} syscall_sched_t;

/**
 * 时钟操作接口
 */
typedef struct {
    long (*clock_gettime)(int clock_id, timespec_t *tp);
} syscall_clock_t;

/**
 * 信号操作接口
 */
typedef struct {
    long (*kill)(int pid, int signum);
    long (*sigaction)(int signum, const void *action, void *old_action);
    long (*sigprocmask)(uintptr_t mask);
    long (*sigreturn)(void);
} syscall_signal_t;

/* 注册处理器 */
void syscall_set_io(const syscall_io_t *io);
void syscall_set_proc(const syscall_proc_t *proc);
void syscall_set_sched(const syscall_sched_t *sched);
void syscall_set_clock(const syscall_clock_t *clock);
void syscall_set_signal(const syscall_signal_t *signal);

/**
 * 线程操作接口
 */
typedef struct {
    long (*thread_create)(uintptr_t entry, uintptr_t arg);
    long (*gettid)(void);
    long (*waittid)(int tid);
} syscall_thread_t;

void syscall_set_thread(const syscall_thread_t *thread);

/**
 * 同步原语接口
 */
typedef struct {
    long (*mutex_create)(int blocking);
    long (*mutex_lock)(int mutex_id);
    long (*mutex_unlock)(int mutex_id);
    long (*semaphore_create)(int res_count);
    long (*semaphore_up)(int sem_id);
    long (*semaphore_down)(int sem_id);
    long (*condvar_create)(int arg);
    long (*condvar_signal)(int condvar_id);
    long (*condvar_wait)(int condvar_id, int mutex_id);
} syscall_sync_t;

void syscall_set_sync(const syscall_sync_t *sync);

/* 处理系统调用 */
syscall_result_t syscall_dispatch(uintptr_t id, uintptr_t args[6]);

#endif /* SYSCALL_H */
