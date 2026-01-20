/**
 * 系统调用处理实现
 */
#include "syscall.h"

static const syscall_io_t *g_io;
static const syscall_proc_t *g_proc;
static const syscall_sched_t *g_sched;
static const syscall_clock_t *g_clock;
static const syscall_signal_t *g_signal;
static const syscall_thread_t *g_thread;
static const syscall_sync_t *g_sync;

void syscall_set_io(const syscall_io_t *io) {
    g_io = io;
}

void syscall_set_proc(const syscall_proc_t *proc) {
    g_proc = proc;
}

void syscall_set_sched(const syscall_sched_t *sched) {
    g_sched = sched;
}

void syscall_set_clock(const syscall_clock_t *clock) {
    g_clock = clock;
}

void syscall_set_signal(const syscall_signal_t *signal) {
    g_signal = signal;
}

void syscall_set_thread(const syscall_thread_t *thread) {
    g_thread = thread;
}

void syscall_set_sync(const syscall_sync_t *sync) {
    g_sync = sync;
}

syscall_result_t syscall_dispatch(uintptr_t id, uintptr_t args[6]) {
    syscall_result_t ret = {.status = SYSCALL_OK, .value = 0};

    switch (id) {
    case SYS_OPEN:
        if (g_io && g_io->open) {
            ret.value = g_io->open((const char *)args[0], args[1]);
        }
        break;

    case SYS_CLOSE:
        if (g_io && g_io->close) {
            ret.value = g_io->close(args[0]);
        }
        break;

    case SYS_READ:
        if (g_io && g_io->read) {
            ret.value = g_io->read(args[0], (void *)args[1], args[2]);
        }
        break;

    case SYS_WRITE:
        if (g_io && g_io->write) {
            ret.value = g_io->write(args[0], (const void *)args[1], args[2]);
        }
        break;

    case SYS_EXIT:
        if (g_proc && g_proc->exit) {
            g_proc->exit(args[0]);
        }
        break;

    case SYS_SCHED_YIELD:
        if (g_sched && g_sched->sched_yield) {
            ret.value = g_sched->sched_yield();
        }
        break;

    case SYS_CLOCK_GETTIME:
        if (g_clock && g_clock->clock_gettime) {
            ret.value = g_clock->clock_gettime(args[0], (timespec_t *)args[1]);
        }
        break;

    case SYS_GETPID:
        if (g_proc && g_proc->getpid) {
            ret.value = g_proc->getpid();
        }
        break;

    case SYS_FORK:
        if (g_proc && g_proc->fork) {
            ret.value = g_proc->fork();
        }
        break;

    case SYS_EXEC:
        if (g_proc && g_proc->exec) {
            ret.value = g_proc->exec((const char *)args[0], args[1]);
        }
        break;

    case SYS_WAITPID:
        if (g_proc && g_proc->waitpid) {
            ret.value = g_proc->waitpid((long)args[0], (int *)args[1]);
        }
        break;

    case SYS_KILL:
        if (g_signal && g_signal->kill) {
            ret.value = g_signal->kill(args[0], args[1]);
        }
        break;

    case SYS_SIGACTION:
        if (g_signal && g_signal->sigaction) {
            ret.value = g_signal->sigaction(args[0], (const void *)args[1], (void *)args[2]);
        }
        break;

    case SYS_SIGPROCMASK:
        if (g_signal && g_signal->sigprocmask) {
            ret.value = g_signal->sigprocmask(args[0]);
        }
        break;

    case SYS_SIGRETURN:
        if (g_signal && g_signal->sigreturn) {
            ret.value = g_signal->sigreturn();
        }
        break;

    /* 线程相关 */
    case SYS_THREAD_CREATE:
        if (g_thread && g_thread->thread_create) {
            ret.value = g_thread->thread_create(args[0], args[1]);
        }
        break;

    case SYS_GETTID:
        if (g_thread && g_thread->gettid) {
            ret.value = g_thread->gettid();
        }
        break;

    case SYS_WAITTID:
        if (g_thread && g_thread->waittid) {
            ret.value = g_thread->waittid(args[0]);
        }
        break;

    /* 同步原语 */
    case SYS_MUTEX_CREATE:
        if (g_sync && g_sync->mutex_create) {
            ret.value = g_sync->mutex_create(args[0]);
        }
        break;

    case SYS_MUTEX_LOCK:
        if (g_sync && g_sync->mutex_lock) {
            ret.value = g_sync->mutex_lock(args[0]);
        }
        break;

    case SYS_MUTEX_UNLOCK:
        if (g_sync && g_sync->mutex_unlock) {
            ret.value = g_sync->mutex_unlock(args[0]);
        }
        break;

    case SYS_SEMAPHORE_CREATE:
        if (g_sync && g_sync->semaphore_create) {
            ret.value = g_sync->semaphore_create(args[0]);
        }
        break;

    case SYS_SEMAPHORE_UP:
        if (g_sync && g_sync->semaphore_up) {
            ret.value = g_sync->semaphore_up(args[0]);
        }
        break;

    case SYS_SEMAPHORE_DOWN:
        if (g_sync && g_sync->semaphore_down) {
            ret.value = g_sync->semaphore_down(args[0]);
        }
        break;

    case SYS_CONDVAR_CREATE:
        if (g_sync && g_sync->condvar_create) {
            ret.value = g_sync->condvar_create(args[0]);
        }
        break;

    case SYS_CONDVAR_SIGNAL:
        if (g_sync && g_sync->condvar_signal) {
            ret.value = g_sync->condvar_signal(args[0]);
        }
        break;

    case SYS_CONDVAR_WAIT:
        if (g_sync && g_sync->condvar_wait) {
            ret.value = g_sync->condvar_wait(args[0], args[1]);
        }
        break;

    default:
        ret.status = SYSCALL_UNSUPPORTED;
        ret.value = id;
        break;
    }

    return ret;
}
