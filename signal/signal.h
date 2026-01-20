/**
 * 信号处理模块
 */
#ifndef SIGNAL_H
#define SIGNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../kernel-context/context.h"

/* ============================================================================
 * 信号编号定义
 * ========================================================================== */

#define MAX_SIG 31

typedef enum {
    SIG_ERR     = 0,
    SIGHUP      = 1,
    SIGINT      = 2,
    SIGQUIT     = 3,
    SIGILL      = 4,
    SIGTRAP     = 5,
    SIGABRT     = 6,
    SIGBUS      = 7,
    SIGFPE      = 8,
    SIGKILL     = 9,
    SIGUSR1     = 10,
    SIGSEGV     = 11,
    SIGUSR2     = 12,
    SIGPIPE     = 13,
    SIGALRM     = 14,
    SIGTERM     = 15,
    SIGSTKFLT   = 16,
    SIGCHLD     = 17,
    SIGCONT     = 18,
    SIGSTOP     = 19,
    SIGTSTP     = 20,
    SIGTTIN     = 21,
    SIGTTOU     = 22,
    SIGURG      = 23,
    SIGXCPU     = 24,
    SIGXFSZ     = 25,
    SIGVTALRM   = 26,
    SIGPROF     = 27,
    SIGWINCH    = 28,
    SIGIO       = 29,
    SIGPWR      = 30,
    SIGSYS      = 31,
} signal_no_t;

/* ============================================================================
 * 信号动作
 * ========================================================================== */

typedef struct {
    uintptr_t handler;  /* 信号处理函数地址 */
    uintptr_t mask;     /* 处理时屏蔽的信号 */
} signal_action_t;

/* ============================================================================
 * 信号处理结果
 * ========================================================================== */

typedef enum {
    SIGNAL_NO_SIGNAL,           /* 没有信号需要处理 */
    SIGNAL_IS_HANDLING,         /* 正在处理信号 */
    SIGNAL_IGNORED,             /* 信号被忽略 */
    SIGNAL_HANDLED,             /* 信号已处理 */
    SIGNAL_PROCESS_KILLED,      /* 进程被杀死 */
    SIGNAL_PROCESS_SUSPENDED,   /* 进程被暂停 */
} signal_result_type_t;

typedef struct {
    signal_result_type_t type;
    int exit_code;
} signal_result_t;

/* ============================================================================
 * 信号集合（位图）
 * ========================================================================== */

typedef struct {
    uint64_t bits;
} signal_set_t;

static inline signal_set_t sigset_empty(void) {
    return (signal_set_t){0};
}

static inline bool sigset_contains(signal_set_t *set, int signum) {
    return (set->bits >> signum) & 1;
}

static inline void sigset_add(signal_set_t *set, int signum) {
    set->bits |= (1ULL << signum);
}

static inline void sigset_remove(signal_set_t *set, int signum) {
    set->bits &= ~(1ULL << signum);
}

static inline uint64_t sigset_set_new(signal_set_t *set, uint64_t new_val) {
    uint64_t old = set->bits;
    set->bits = new_val;
    return old;
}

/* 查找第一个未被屏蔽的信号 */
static inline int sigset_find_first(signal_set_t *set, signal_set_t *mask) {
    uint64_t pending = set->bits & ~mask->bits;
    if (pending == 0) return -1;
    /* 找到最低位的 1 */
    int pos = 0;
    if ((pending & 0xFFFFFFFF) == 0) { pos += 32; pending >>= 32; }
    if ((pending & 0xFFFF) == 0) { pos += 16; pending >>= 16; }
    if ((pending & 0xFF) == 0) { pos += 8; pending >>= 8; }
    if ((pending & 0xF) == 0) { pos += 4; pending >>= 4; }
    if ((pending & 0x3) == 0) { pos += 2; pending >>= 2; }
    if ((pending & 0x1) == 0) { pos += 1; }
    return pos;
}

/* ============================================================================
 * 正在处理的信号
 * ========================================================================== */

typedef enum {
    HANDLING_NONE,
    HANDLING_FROZEN,        /* 进程被暂停 */
    HANDLING_USER_SIGNAL,   /* 用户信号处理 */
} handling_type_t;

typedef struct {
    handling_type_t type;
    context_t saved_ctx;    /* 保存的用户上下文（仅 USER_SIGNAL 时有效） */
} handling_signal_t;

/* ============================================================================
 * 信号管理器
 * ========================================================================== */

typedef struct {
    signal_set_t received;                      /* 已收到的信号 */
    signal_set_t mask;                          /* 信号掩码 */
    handling_signal_t handling;                 /* 正在处理的信号 */
    signal_action_t actions[MAX_SIG + 1];       /* 信号处理函数 */
    bool action_set[MAX_SIG + 1];               /* 是否设置了处理函数 */
} signal_manager_t;

/* 初始化信号管理器 */
void signal_init(signal_manager_t *sm);

/* 复制信号管理器（用于 fork） */
void signal_fork(signal_manager_t *dst, signal_manager_t *src);

/* 清除信号处理函数（用于 exec） */
void signal_clear(signal_manager_t *sm);

/* 添加信号 */
void signal_add(signal_manager_t *sm, signal_no_t signum);

/* 是否正在处理信号 */
bool signal_is_handling(signal_manager_t *sm);

/* 设置信号处理函数 */
bool signal_set_action(signal_manager_t *sm, signal_no_t signum, const signal_action_t *action);

/* 获取信号处理函数 */
bool signal_get_action(signal_manager_t *sm, signal_no_t signum, signal_action_t *action);

/* 更新信号掩码 */
uint64_t signal_update_mask(signal_manager_t *sm, uint64_t new_mask);

/* 处理信号 */
signal_result_t signal_handle(signal_manager_t *sm, context_t *ctx);

/* 从信号处理返回 */
bool signal_return(signal_manager_t *sm, context_t *ctx);

#endif /* SIGNAL_H */
