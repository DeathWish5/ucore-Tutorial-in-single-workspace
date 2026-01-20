/**
 * 同步原语模块
 *
 * 提供 Semaphore, Mutex, Condvar
 */
#ifndef SYNC_H
#define SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 线程 ID */
typedef uint32_t tid_t;
#define TID_INVALID ((tid_t)-1)

/* ============================================================================
 * 等待队列（简单实现）
 * ========================================================================== */

#define WAIT_QUEUE_SIZE 16

typedef struct {
    tid_t queue[WAIT_QUEUE_SIZE];
    int head;
    int tail;
    int count;
} wait_queue_t;

static inline void wq_init(wait_queue_t *wq) {
    wq->head = 0;
    wq->tail = 0;
    wq->count = 0;
}

static inline bool wq_push(wait_queue_t *wq, tid_t tid) {
    if (wq->count >= WAIT_QUEUE_SIZE) return false;
    wq->queue[wq->tail] = tid;
    wq->tail = (wq->tail + 1) % WAIT_QUEUE_SIZE;
    wq->count++;
    return true;
}

static inline tid_t wq_pop(wait_queue_t *wq) {
    if (wq->count == 0) return TID_INVALID;
    tid_t tid = wq->queue[wq->head];
    wq->head = (wq->head + 1) % WAIT_QUEUE_SIZE;
    wq->count--;
    return tid;
}

/* ============================================================================
 * 信号量
 * ========================================================================== */

typedef struct {
    int count;
    wait_queue_t wait_queue;
} semaphore_t;

/* 初始化信号量 */
void sem_init(semaphore_t *sem, int initial_count);

/* P 操作（down），返回 true 表示成功获取，false 表示需要阻塞 */
bool sem_down(semaphore_t *sem, tid_t tid);

/* V 操作（up），返回被唤醒的线程 ID（如果有） */
tid_t sem_up(semaphore_t *sem);

/* ============================================================================
 * 互斥锁
 * ========================================================================== */

typedef struct {
    bool locked;
    wait_queue_t wait_queue;
} mutex_t;

/* 初始化互斥锁 */
void mutex_init(mutex_t *mtx);

/* 加锁，返回 true 表示成功，false 表示需要阻塞 */
bool mutex_lock(mutex_t *mtx, tid_t tid);

/* 解锁，返回被唤醒的线程 ID（如果有） */
tid_t mutex_unlock(mutex_t *mtx);

/* ============================================================================
 * 条件变量
 * ========================================================================== */

typedef struct {
    wait_queue_t wait_queue;
} condvar_t;

/* 初始化条件变量 */
void condvar_init(condvar_t *cv);

/* 等待（阻塞当前线程），返回 false 表示需要阻塞 */
bool condvar_wait(condvar_t *cv, tid_t tid);

/* 唤醒一个等待的线程，返回被唤醒的线程 ID */
tid_t condvar_signal(condvar_t *cv);

/* 带互斥锁的等待，返回 (需要阻塞?, 被唤醒的线程ID) */
typedef struct {
    bool need_block;
    tid_t waking_tid;
} condvar_wait_result_t;

condvar_wait_result_t condvar_wait_with_mutex(condvar_t *cv, mutex_t *mtx, tid_t tid);

#endif /* SYNC_H */
