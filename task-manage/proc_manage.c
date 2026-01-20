/**
 * 进程管理器实现
 */
#include "proc_manage.h"
#include <string.h>

/* 全局 PID 计数器 */
static pid_t g_pid_counter = 0;

pid_t pid_alloc(void) {
    return g_pid_counter++;
}

void pm_init(proc_manager_t *pm) {
    memset(pm, 0, sizeof(*pm));
    pm->current = PID_INVALID;
    pm->queue_head = 0;
    pm->queue_tail = 0;
}

/* 内部：添加到就绪队列 */
static void queue_push(proc_manager_t *pm, pid_t pid) {
    pm->ready_queue[pm->queue_tail] = pid;
    pm->queue_tail = (pm->queue_tail + 1) % MAX_PROCS;
}

/* 内部：从就绪队列取出 */
static pid_t queue_pop(proc_manager_t *pm) {
    if (pm->queue_head == pm->queue_tail) {
        return PID_INVALID;
    }
    pid_t pid = pm->ready_queue[pm->queue_head];
    pm->queue_head = (pm->queue_head + 1) % MAX_PROCS;
    return pid;
}

void pm_add(proc_manager_t *pm, pid_t pid, struct process *proc, pid_t parent) {
    if (pid >= MAX_PROCS) return;

    pm->procs[pid] = proc;

    /* 初始化进程关系 */
    proc_rel_t *rel = &pm->relations[pid];
    memset(rel, 0, sizeof(*rel));
    rel->parent = parent;

    /* 添加到父进程的子列表 */
    if (parent != PID_INVALID && parent < MAX_PROCS) {
        proc_rel_t *parent_rel = &pm->relations[parent];
        if (parent_rel->child_count < MAX_CHILDREN) {
            parent_rel->children[parent_rel->child_count++] = pid;
        }
    }

    /* 加入就绪队列 */
    queue_push(pm, pid);
}

struct process *pm_find_next(proc_manager_t *pm) {
    pid_t pid = queue_pop(pm);
    if (pid == PID_INVALID) {
        return NULL;
    }
    pm->current = pid;
    return pm->procs[pid];
}

struct process *pm_current(proc_manager_t *pm) {
    if (pm->current == PID_INVALID) return NULL;
    return pm->procs[pm->current];
}

struct process *pm_get(proc_manager_t *pm, pid_t pid) {
    if (pid >= MAX_PROCS) return NULL;
    return pm->procs[pid];
}

void pm_suspend_current(proc_manager_t *pm) {
    if (pm->current != PID_INVALID) {
        queue_push(pm, pm->current);
        pm->current = PID_INVALID;
    }
}

void pm_exit_current(proc_manager_t *pm, int exit_code) {
    pid_t pid = pm->current;
    if (pid == PID_INVALID) return;

    proc_rel_t *rel = &pm->relations[pid];
    pid_t parent = rel->parent;

    /* 通知父进程 */
    if (parent != PID_INVALID && parent < MAX_PROCS) {
        proc_rel_t *parent_rel = &pm->relations[parent];

        /* 从父进程的子列表中移除 */
        for (size_t i = 0; i < parent_rel->child_count; i++) {
            if (parent_rel->children[i] == pid) {
                /* 移到 dead_children */
                if (parent_rel->dead_count < MAX_CHILDREN) {
                    parent_rel->dead_children[parent_rel->dead_count].pid = pid;
                    parent_rel->dead_children[parent_rel->dead_count].exit_code = exit_code;
                    parent_rel->dead_count++;
                }
                /* 从 children 中删除 */
                for (size_t j = i; j < parent_rel->child_count - 1; j++) {
                    parent_rel->children[j] = parent_rel->children[j + 1];
                }
                parent_rel->child_count--;
                break;
            }
        }
    }

    /* 把子进程转移给 init (pid 0) */
    for (size_t i = 0; i < rel->child_count; i++) {
        pid_t child = rel->children[i];
        if (child < MAX_PROCS) {
            pm->relations[child].parent = 0;
            /* 添加到 init 的子列表 */
            proc_rel_t *init_rel = &pm->relations[0];
            if (init_rel->child_count < MAX_CHILDREN) {
                init_rel->children[init_rel->child_count++] = child;
            }
        }
    }

    /* 清理进程 */
    pm->procs[pid] = NULL;
    memset(rel, 0, sizeof(*rel));
    pm->current = PID_INVALID;
}

pid_t pm_current_pid(proc_manager_t *pm) {
    return pm->current;
}

wait_result_t pm_wait(proc_manager_t *pm, pid_t child_pid) {
    wait_result_t result = {PID_INVALID, -1, false};

    pid_t current = pm->current;
    if (current == PID_INVALID) return result;

    proc_rel_t *rel = &pm->relations[current];

    if (child_pid == PID_INVALID) {
        /* 等待任意子进程 */
        if (rel->dead_count > 0) {
            rel->dead_count--;
            result.pid = rel->dead_children[rel->dead_count].pid;
            result.exit_code = rel->dead_children[rel->dead_count].exit_code;
            result.found = true;
        } else if (rel->child_count > 0) {
            /* 有子进程但还没退出 */
            result.pid = (pid_t)-2;
            result.exit_code = -1;
            result.found = true;
        }
        /* 否则没有子进程，返回 found=false */
    } else {
        /* 等待特定子进程 */
        /* 先检查 dead_children */
        for (size_t i = 0; i < rel->dead_count; i++) {
            if (rel->dead_children[i].pid == child_pid) {
                result.pid = rel->dead_children[i].pid;
                result.exit_code = rel->dead_children[i].exit_code;
                result.found = true;
                /* 移除 */
                for (size_t j = i; j < rel->dead_count - 1; j++) {
                    rel->dead_children[j] = rel->dead_children[j + 1];
                }
                rel->dead_count--;
                return result;
            }
        }
        /* 检查 children (还在运行) */
        for (size_t i = 0; i < rel->child_count; i++) {
            if (rel->children[i] == child_pid) {
                result.pid = (pid_t)-2;
                result.exit_code = -1;
                result.found = true;
                return result;
            }
        }
        /* 不存在 */
    }

    return result;
}
