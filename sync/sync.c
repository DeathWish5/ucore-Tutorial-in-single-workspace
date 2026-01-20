/**
 * 同步原语实现
 */
#include "sync.h"

/* ============================================================================
 * 信号量
 * ========================================================================== */

void sem_init(semaphore_t *sem, int initial_count) {
    sem->count = initial_count;
    wq_init(&sem->wait_queue);
}

bool sem_down(semaphore_t *sem, tid_t tid) {
    sem->count--;
    if (sem->count < 0) {
        wq_push(&sem->wait_queue, tid);
        return false;  /* 需要阻塞 */
    }
    return true;  /* 成功获取 */
}

tid_t sem_up(semaphore_t *sem) {
    sem->count++;
    return wq_pop(&sem->wait_queue);
}

/* ============================================================================
 * 互斥锁
 * ========================================================================== */

void mutex_init(mutex_t *mtx) {
    mtx->locked = false;
    wq_init(&mtx->wait_queue);
}

bool mutex_lock(mutex_t *mtx, tid_t tid) {
    if (mtx->locked) {
        wq_push(&mtx->wait_queue, tid);
        return false;  /* 需要阻塞 */
    }
    mtx->locked = true;
    return true;  /* 成功获取 */
}

tid_t mutex_unlock(mutex_t *mtx) {
    tid_t waking = wq_pop(&mtx->wait_queue);
    if (waking == TID_INVALID) {
        mtx->locked = false;
    }
    /* 如果有等待者，锁保持 locked 状态 */
    return waking;
}

/* ============================================================================
 * 条件变量
 * ========================================================================== */

void condvar_init(condvar_t *cv) {
    wq_init(&cv->wait_queue);
}

bool condvar_wait(condvar_t *cv, tid_t tid) {
    wq_push(&cv->wait_queue, tid);
    return false;  /* 总是需要阻塞 */
}

tid_t condvar_signal(condvar_t *cv) {
    return wq_pop(&cv->wait_queue);
}

condvar_wait_result_t condvar_wait_with_mutex(condvar_t *cv, mutex_t *mtx, tid_t tid) {
    condvar_wait_result_t result;

    /* 释放互斥锁，唤醒一个等待者 */
    result.waking_tid = mutex_unlock(mtx);

    /* 当前线程尝试重新获取锁 */
    result.need_block = !mutex_lock(mtx, tid);

    return result;
}
