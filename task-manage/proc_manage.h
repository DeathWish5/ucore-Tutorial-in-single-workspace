/**
 * 进程管理器
 *
 * 管理进程列表、父子关系、调度队列
 */
#ifndef PROC_MANAGE_H
#define PROC_MANAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * 进程 ID
 * ========================================================================== */

typedef size_t pid_t;

#define PID_INVALID ((pid_t)-1)

/* 分配新的进程 ID */
pid_t pid_alloc(void);

/* ============================================================================
 * 进程关系
 * ========================================================================== */

#define MAX_CHILDREN 32

typedef struct {
    pid_t parent;
    pid_t children[MAX_CHILDREN];
    size_t child_count;
    /* 已退出的子进程 */
    struct {
        pid_t pid;
        int exit_code;
    } dead_children[MAX_CHILDREN];
    size_t dead_count;
} proc_rel_t;

/* ============================================================================
 * 进程管理器
 * ========================================================================== */

#define MAX_PROCS 64

/* 进程结构体前向声明（由具体 ch 定义） */
struct process;

typedef struct {
    /* 进程存储 */
    struct process *procs[MAX_PROCS];
    /* 进程关系 */
    proc_rel_t relations[MAX_PROCS];
    /* 调度队列 */
    pid_t ready_queue[MAX_PROCS];
    size_t queue_head;
    size_t queue_tail;
    /* 当前进程 */
    pid_t current;
} proc_manager_t;

/* 初始化进程管理器 */
void pm_init(proc_manager_t *pm);

/* 添加进程（指定父进程） */
void pm_add(proc_manager_t *pm, pid_t pid, struct process *proc, pid_t parent);

/* 获取下一个可运行进程 */
struct process *pm_find_next(proc_manager_t *pm);

/* 获取当前进程 */
struct process *pm_current(proc_manager_t *pm);

/* 获取进程 */
struct process *pm_get(proc_manager_t *pm, pid_t pid);

/* 将当前进程挂起（放回就绪队列） */
void pm_suspend_current(proc_manager_t *pm);

/* 结束当前进程 */
void pm_exit_current(proc_manager_t *pm, int exit_code);

/* 获取当前进程 PID */
pid_t pm_current_pid(proc_manager_t *pm);

/* wait 系统调用：等待子进程退出 */
typedef struct {
    pid_t pid;
    int exit_code;
    bool found;
} wait_result_t;

wait_result_t pm_wait(proc_manager_t *pm, pid_t child_pid);

#endif /* PROC_MANAGE_H */
