/**
 * 用户态库
 */
#ifndef USER_H
#define USER_H

#include <stddef.h>
#include <stdint.h>

/* 标准文件描述符 */
#define STDIN   0
#define STDOUT  1
#define STDERR  2

/* 时钟类型 */
#define CLOCK_REALTIME      0
#define CLOCK_MONOTONIC     1

/* 时间结构 */
typedef struct {
    uintptr_t tv_sec;
    uintptr_t tv_nsec;
} timespec_t;

/* 系统调用 */
int sys_open(const char *path, unsigned int flags);
int sys_close(int fd);
int sys_read(int fd, void *buf, size_t count);
int sys_write(int fd, const void *buf, size_t count);
void sys_exit(int code) __attribute__((noreturn));
int sys_sched_yield(void);
int sys_clock_gettime(int clock_id, timespec_t *tp);
int sys_getpid(void);
int sys_fork(void);
int sys_exec(const char *path, size_t len);
int sys_waitpid(int pid, int *exit_code);
int sys_kill(int pid, int signum);
int sys_sigaction(int signum, const void *action, void *old_action);
int sys_sigprocmask(unsigned long mask);
int sys_sigreturn(void);

/* 打开标志 */
#define O_RDONLY    0
#define O_WRONLY    (1 << 0)
#define O_RDWR      (1 << 1)
#define O_CREATE    (1 << 9)
#define O_TRUNC     (1 << 10)

/* 信号编号 */
#define SIGINT      2
#define SIGKILL     9
#define SIGUSR1     10
#define SIGCONT     18
#define SIGSTOP     19

/* 信号动作结构 */
typedef struct {
    unsigned long handler;
    unsigned long mask;
} sigaction_t;

/* 线程相关 */
int sys_thread_create(void (*entry)(unsigned long), unsigned long arg);
int sys_gettid(void);
int sys_waittid(int tid);

/* 同步原语 */
int sys_mutex_create(void);
int sys_mutex_blocking_create(void);
int sys_mutex_lock(int mutex_id);
int sys_mutex_unlock(int mutex_id);
int sys_semaphore_create(int res_count);
int sys_semaphore_up(int sem_id);
int sys_semaphore_down(int sem_id);
int sys_condvar_create(void);
int sys_condvar_signal(int condvar_id);
int sys_condvar_wait(int condvar_id, int mutex_id);

/* 便捷封装 */
static inline int getchar(void) {
    char c;
    sys_read(STDIN, &c, 1);
    return c;
}

static inline int wait(int *exit_code) {
    return sys_waitpid(-1, exit_code);
}

/* 输出函数 */
void putchar(char c);
void puts(const char *s);
void print_str(const char *s);
void print_int(unsigned int n);
void print_long(unsigned long n);

/* 字符串操作 */
size_t strlen(const char *s);

#endif /* USER_H */
