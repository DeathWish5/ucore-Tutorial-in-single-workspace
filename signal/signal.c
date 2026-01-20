/**
 * 信号处理实现
 */
#include "signal.h"
#include <string.h>

void signal_init(signal_manager_t *sm) {
    memset(sm, 0, sizeof(signal_manager_t));
    sm->handling.type = HANDLING_NONE;
}

void signal_fork(signal_manager_t *dst, signal_manager_t *src) {
    dst->received = sigset_empty();
    dst->mask = src->mask;
    dst->handling.type = HANDLING_NONE;
    memcpy(dst->actions, src->actions, sizeof(dst->actions));
    memcpy(dst->action_set, src->action_set, sizeof(dst->action_set));
}

void signal_clear(signal_manager_t *sm) {
    memset(sm->actions, 0, sizeof(sm->actions));
    memset(sm->action_set, 0, sizeof(sm->action_set));
}

void signal_add(signal_manager_t *sm, signal_no_t signum) {
    sigset_add(&sm->received, signum);
}

bool signal_is_handling(signal_manager_t *sm) {
    return sm->handling.type != HANDLING_NONE;
}

bool signal_set_action(signal_manager_t *sm, signal_no_t signum, const signal_action_t *action) {
    /* SIGKILL 和 SIGSTOP 不能被捕获 */
    if (signum == SIGKILL || signum == SIGSTOP) {
        return false;
    }
    if (signum > MAX_SIG) {
        return false;
    }
    sm->actions[signum] = *action;
    sm->action_set[signum] = true;
    return true;
}

bool signal_get_action(signal_manager_t *sm, signal_no_t signum, signal_action_t *action) {
    if (signum == SIGKILL || signum == SIGSTOP) {
        return false;
    }
    if (signum > MAX_SIG) {
        return false;
    }
    if (sm->action_set[signum]) {
        *action = sm->actions[signum];
    } else {
        action->handler = 0;
        action->mask = 0;
    }
    return true;
}

uint64_t signal_update_mask(signal_manager_t *sm, uint64_t new_mask) {
    return sigset_set_new(&sm->mask, new_mask);
}

/* 默认动作 */
static signal_result_t default_action(signal_no_t signum) {
    signal_result_t result;

    switch (signum) {
    case SIGCHLD:
    case SIGURG:
        /* 忽略 */
        result.type = SIGNAL_IGNORED;
        result.exit_code = 0;
        break;
    default:
        /* 终止进程 */
        result.type = SIGNAL_PROCESS_KILLED;
        result.exit_code = -(int)signum;
        break;
    }
    return result;
}

signal_result_t signal_handle(signal_manager_t *sm, context_t *ctx) {
    signal_result_t result = {SIGNAL_NO_SIGNAL, 0};

    if (signal_is_handling(sm)) {
        if (sm->handling.type == HANDLING_FROZEN) {
            /* 检查是否收到 SIGCONT */
            if (sigset_contains(&sm->received, SIGCONT) &&
                !sigset_contains(&sm->mask, SIGCONT)) {
                sigset_remove(&sm->received, SIGCONT);
                sm->handling.type = HANDLING_NONE;
                result.type = SIGNAL_HANDLED;
            } else {
                result.type = SIGNAL_PROCESS_SUSPENDED;
            }
        } else {
            /* 正在处理用户信号 */
            result.type = SIGNAL_IS_HANDLING;
        }
        return result;
    }

    /* 查找未被屏蔽的信号 */
    int signum = sigset_find_first(&sm->received, &sm->mask);
    if (signum < 0) {
        return result;  /* 没有信号 */
    }

    /* 移除该信号 */
    sigset_remove(&sm->received, signum);

    signal_no_t sig = (signal_no_t)signum;

    switch (sig) {
    case SIGKILL:
        /* SIGKILL 不能被捕获 */
        result.type = SIGNAL_PROCESS_KILLED;
        result.exit_code = -SIGKILL;
        break;

    case SIGSTOP:
        /* SIGSTOP 暂停进程 */
        sm->handling.type = HANDLING_FROZEN;
        result.type = SIGNAL_PROCESS_SUSPENDED;
        break;

    default:
        if (sm->action_set[signum] && sm->actions[signum].handler != 0) {
            /* 用户定义的处理函数 */
            sm->handling.type = HANDLING_USER_SIGNAL;
            sm->handling.saved_ctx = *ctx;

            /* 修改上下文：跳转到处理函数，a0 = 信号编号 */
            ctx_set_pc(ctx, sm->actions[signum].handler);
            ctx_set_arg(ctx, 0, signum);

            result.type = SIGNAL_HANDLED;
        } else {
            /* 默认动作 */
            result = default_action(sig);
        }
        break;
    }

    return result;
}

bool signal_return(signal_manager_t *sm, context_t *ctx) {
    if (sm->handling.type == HANDLING_USER_SIGNAL) {
        /* 恢复保存的上下文 */
        *ctx = sm->handling.saved_ctx;
        sm->handling.type = HANDLING_NONE;
        return true;
    }
    return false;
}
