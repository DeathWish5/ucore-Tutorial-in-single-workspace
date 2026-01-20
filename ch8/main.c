/**
 * ch8 - 线程与同步
 *
 * 在 ch7 基础上增加线程和同步原语支持
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../kernel-alloc/heap.h"
#include "../kernel-context/context.h"
#include "../kernel-vm/address_space.h"
#include "../kernel-vm/elf.h"
#include "../kernel-vm/sv39.h"
#include "../linker/linker.h"
#include "../syscall/syscall.h"
#include "../util/printf.h"
#include "../util/riscv.h"
#include "../util/sbi.h"
#include "../easy-fs/easy_fs.h"
#include "../virtio-block/virtio_block.h"
#include "../signal/signal.h"
#include "../sync/sync.h"

/* ============================================================================
 * 配置
 * ========================================================================== */

#define MEMORY_SIZE     (48 << 20)
#define USER_STACK_SIZE (2 * PAGE_SIZE)
#define USER_STACK_TOP  (1UL << 38)
#define MAX_FD          16
#define MAX_THREADS     16
#define MAX_SYNC_OBJS   16

#define VIRTIO_MMIO_BASE 0x10001000
#define VIRTIO_MMIO_SIZE 0x1000

/* ============================================================================
 * 全局状态
 * ========================================================================== */

static kernel_layout_t g_layout;
static uintptr_t g_memory_end;
static address_space_t *kernel_as;

static virtio_blk_t g_virtio_blk;
static block_device_t *g_block_dev;
static easy_fs_t *g_fs;
static inode_t *g_root;

/* ============================================================================
 * 线程和进程结构
 * ========================================================================== */

typedef uint32_t pid_t;
#define PID_INVALID ((pid_t)-1)
#define MAX_PROCS 16

/* 线程 */
typedef struct thread {
    tid_t tid;
    pid_t pid;          /* 所属进程 */
    foreign_ctx_t ctx;
    int exit_code;
    bool exited;
} thread_t;

/* 进程 */
typedef struct process {
    pid_t pid;
    address_space_t *as;
    file_handle_t *fd_table[MAX_FD];
    signal_manager_t signal;
    /* 线程列表 */
    tid_t threads[MAX_THREADS];
    int thread_count;
    /* 同步原语 */
    semaphore_t *semaphores[MAX_SYNC_OBJS];
    mutex_t *mutexes[MAX_SYNC_OBJS];
    condvar_t *condvars[MAX_SYNC_OBJS];
    /* 父子关系 */
    pid_t parent;
    int exit_code;
    bool exited;
    /* 等待子进程 */
    pid_t waiting_for;  /* 正在等待的子进程 pid，-1 表示任意，PID_INVALID 表示不在等待 */
    tid_t waiting_tid;  /* 等待中的线程 */
    int *waiting_exit_code_ptr;  /* 内核地址空间中的 exit_code 指针 */
} process_t;

static thread_t g_thread_pool[MAX_PROCS * MAX_THREADS];
static process_t g_process_pool[MAX_PROCS];
static tid_t g_next_tid = 0;
static pid_t g_next_pid = 0;

/* 调度队列 */
#define READY_QUEUE_SIZE 64
static tid_t g_ready_queue[READY_QUEUE_SIZE];
static int g_ready_head = 0, g_ready_tail = 0, g_ready_count = 0;
static tid_t g_current_tid = TID_INVALID;

/* ============================================================================
 * 调度器
 * ========================================================================== */

static void ready_enqueue(tid_t tid) {
    if (g_ready_count < READY_QUEUE_SIZE) {
        g_ready_queue[g_ready_tail] = tid;
        g_ready_tail = (g_ready_tail + 1) % READY_QUEUE_SIZE;
        g_ready_count++;
    }
}

static tid_t ready_dequeue(void) {
    if (g_ready_count == 0) return TID_INVALID;
    tid_t tid = g_ready_queue[g_ready_head];
    g_ready_head = (g_ready_head + 1) % READY_QUEUE_SIZE;
    g_ready_count--;
    return tid;
}

static thread_t *get_thread(tid_t tid) {
    if (tid >= MAX_PROCS * MAX_THREADS) return NULL;
    return &g_thread_pool[tid];
}

static process_t *get_process(pid_t pid) {
    if (pid >= MAX_PROCS) return NULL;
    return &g_process_pool[pid];
}

static thread_t *current_thread(void) {
    return get_thread(g_current_tid);
}

static process_t *current_process(void) {
    thread_t *t = current_thread();
    if (!t) return NULL;
    return get_process(t->pid);
}

/* ============================================================================
 * 辅助函数
 * ========================================================================== */

static void map_kernel_to_user(address_space_t *user_as) {
    as_map_extern(user_as, va_vpn(g_layout.text), va_vpn(g_layout.rodata),
                  pa_ppn(g_layout.text), PTE_V | PTE_R | PTE_X);
    as_map_extern(user_as, va_vpn(g_layout.rodata), va_vpn(g_layout.data),
                  pa_ppn(g_layout.rodata), PTE_V | PTE_R);
    as_map_extern(user_as, va_vpn(g_layout.data), va_vpn(g_memory_end),
                  pa_ppn(g_layout.data), PTE_V | PTE_R | PTE_W);
    as_map_extern(user_as, va_vpn(VIRTIO_MMIO_BASE),
                  va_vpn(VIRTIO_MMIO_BASE + VIRTIO_MMIO_SIZE),
                  pa_ppn(VIRTIO_MMIO_BASE), PTE_V | PTE_R | PTE_W);
}

static uint8_t *read_all_file(file_handle_t *fh, size_t *out_len) {
    if (!fh || !fh->inode) return NULL;
    uint32_t size = inode_size(fh->inode);
    uint8_t *data = heap_alloc(size, 8);
    if (!data) return NULL;
    size_t total = 0;
    uint8_t buf[512];
    while (total < size) {
        size_t n = file_read(fh, buf, 512);
        if (n == 0) break;
        memcpy(data + total, buf, n);
        total += n;
    }
    *out_len = total;
    return data;
}

/* ============================================================================
 * 进程/线程创建
 * ========================================================================== */

static tid_t alloc_tid(void) {
    return g_next_tid++;
}

static pid_t alloc_pid(void) {
    return g_next_pid++;
}

static thread_t *create_thread(pid_t pid, uintptr_t entry, uintptr_t sp, uintptr_t satp) {
    tid_t tid = alloc_tid();
    if (tid >= MAX_PROCS * MAX_THREADS) return NULL;

    thread_t *t = &g_thread_pool[tid];
    t->tid = tid;
    t->pid = pid;
    t->ctx.ctx = context_user(entry);
    t->ctx.satp = satp;
    ctx_set_sp(&t->ctx.ctx, sp);
    t->exit_code = 0;
    t->exited = false;

    return t;
}

static bool create_process_from_elf(const uint8_t *elf_data, size_t elf_len,
                                    process_t **out_proc, thread_t **out_thread) {
    pid_t pid = alloc_pid();
    if (pid >= MAX_PROCS) return false;

    process_t *proc = &g_process_pool[pid];
    memset(proc, 0, sizeof(process_t));
    proc->pid = pid;

    proc->as = as_create();
    if (!proc->as) return false;

    map_kernel_to_user(proc->as);

    uintptr_t entry = elf_load(proc->as, elf_data, elf_len);
    if (!entry) {
        as_destroy(proc->as);
        return false;
    }

    uintptr_t stack_vpn_end = va_vpn(USER_STACK_TOP);
    uintptr_t stack_vpn_start = stack_vpn_end - (USER_STACK_SIZE / PAGE_SIZE);
    as_map(proc->as, stack_vpn_start, stack_vpn_end, NULL, 0, 0,
           PTE_V | PTE_R | PTE_W | PTE_U);

    uintptr_t satp = make_satp(as_root_ppn(proc->as));

    /* 初始化 fd_table */
    proc->fd_table[0] = heap_alloc(sizeof(file_handle_t), 8);
    proc->fd_table[0]->inode = NULL;
    proc->fd_table[0]->readable = true;
    proc->fd_table[0]->writable = false;
    proc->fd_table[1] = heap_alloc(sizeof(file_handle_t), 8);
    proc->fd_table[1]->inode = NULL;
    proc->fd_table[1]->readable = false;
    proc->fd_table[1]->writable = true;

    signal_init(&proc->signal);
    proc->parent = PID_INVALID;
    proc->exited = false;
    proc->waiting_for = PID_INVALID;
    proc->waiting_tid = TID_INVALID;
    proc->waiting_exit_code_ptr = NULL;

    /* 创建主线程 */
    thread_t *t = create_thread(pid, entry, USER_STACK_TOP, satp);
    if (!t) return false;

    proc->threads[0] = t->tid;
    proc->thread_count = 1;

    *out_proc = proc;
    *out_thread = t;
    return true;
}

/* ============================================================================
 * 系统调用实现
 * ========================================================================== */

static long do_open(const char *path, uint32_t flags) {
    process_t *proc = current_process();
    if (!proc) return -1;

    const char *kpath = as_translate(proc->as, (vaddr_t)path, PTE_R | PTE_V);
    if (!kpath) return -1;

    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!proc->fd_table[i]) { fd = i; break; }
    }
    if (fd < 0) return -1;

    file_handle_t *fh = file_open(g_fs, kpath, flags);
    if (!fh) return -1;

    proc->fd_table[fd] = fh;
    return fd;
}

static long do_close(int fd) {
    process_t *proc = current_process();
    if (!proc || fd < 0 || fd >= MAX_FD) return -1;
    if (proc->fd_table[fd]) {
        file_close(proc->fd_table[fd]);
        proc->fd_table[fd] = NULL;
        return 0;
    }
    return -1;
}

static long do_write(int fd, const void *buf, size_t count) {
    process_t *proc = current_process();
    if (!proc) return -1;

    const char *kbuf = as_translate(proc->as, (vaddr_t)buf, PTE_R | PTE_V);
    if (!kbuf) return -1;

    if (fd == FD_STDOUT || fd == FD_STDERR) {
        for (size_t i = 0; i < count; i++) console_putchar(kbuf[i]);
        return count;
    }

    if (fd < 0 || fd >= MAX_FD || !proc->fd_table[fd]) return -1;
    file_handle_t *fh = proc->fd_table[fd];
    if (!fh->writable) return -1;
    return file_write(fh, (const uint8_t *)kbuf, count);
}

static long do_read(int fd, void *buf, size_t count) {
    process_t *proc = current_process();
    if (!proc) return -1;

    char *kbuf = as_translate(proc->as, (vaddr_t)buf, PTE_W | PTE_V);
    if (!kbuf) return -1;

    if (fd == FD_STDIN) {
        for (size_t i = 0; i < count; i++) {
            int c;
            /* 等待有效输入（console_getchar 在无输入时返回 -1） */
            while ((c = console_getchar()) < 0) {
                /* 忙等待 */
            }
            kbuf[i] = (char)c;
        }
        return count;
    }

    if (fd < 0 || fd >= MAX_FD || !proc->fd_table[fd]) return -1;
    file_handle_t *fh = proc->fd_table[fd];
    if (!fh->readable) return -1;
    return file_read(fh, (uint8_t *)kbuf, count);
}

static void do_exit(int code) { (void)code; }
static long do_sched_yield(void) { return 0; }
static long do_getpid(void) { process_t *p = current_process(); return p ? p->pid : -1; }

static long do_clock_gettime(int clock_id, timespec_t *tp) {
    if (clock_id == CLOCK_MONOTONIC && tp) {
        process_t *proc = current_process();
        if (!proc) return -1;
        timespec_t *ktp = as_translate(proc->as, (vaddr_t)tp, PTE_W | PTE_V);
        if (!ktp) return -1;
        uint64_t time = read_time();
        uint64_t ns = time * 80;
        ktp->tv_sec = ns / 1000000000UL;
        ktp->tv_nsec = ns % 1000000000UL;
        return 0;
    }
    return -1;
}

static long do_fork(void) {
    process_t *parent = current_process();
    thread_t *parent_thread = current_thread();
    if (!parent || !parent_thread) return -1;

    pid_t pid = alloc_pid();
    if (pid >= MAX_PROCS) return -1;

    process_t *child = &g_process_pool[pid];
    memset(child, 0, sizeof(process_t));
    child->pid = pid;
    child->as = as_clone(parent->as);
    if (!child->as) return -1;

    /* 复制 fd_table */
    for (int i = 0; i < MAX_FD; i++) {
        if (parent->fd_table[i]) {
            child->fd_table[i] = heap_alloc(sizeof(file_handle_t), 8);
            memcpy(child->fd_table[i], parent->fd_table[i], sizeof(file_handle_t));
            if (parent->fd_table[i]->inode) {
                child->fd_table[i]->inode = heap_alloc(sizeof(inode_t), 8);
                memcpy(child->fd_table[i]->inode, parent->fd_table[i]->inode, sizeof(inode_t));
            }
        }
    }

    signal_fork(&child->signal, &parent->signal);
    child->parent = parent->pid;

    /* 创建子线程 */
    uintptr_t satp = make_satp(as_root_ppn(child->as));
    thread_t *child_thread = create_thread(pid, 0, 0, satp);
    if (!child_thread) return -1;

    child_thread->ctx.ctx = parent_thread->ctx.ctx;
    child_thread->ctx.satp = satp;
    ctx_set_arg(&child_thread->ctx.ctx, 0, 0);

    child->threads[0] = child_thread->tid;
    child->thread_count = 1;

    ready_enqueue(child_thread->tid);
    return pid;
}

static long do_exec(const char *path, size_t len) {
    process_t *proc = current_process();
    if (!proc) return -1;

    const char *kpath = as_translate(proc->as, (vaddr_t)path, PTE_R | PTE_V);
    if (!kpath) return -1;

    char name[32];
    if (len > 31) len = 31;
    memcpy(name, kpath, len);
    name[len] = '\0';

    file_handle_t *fh = file_open(g_fs, name, O_RDONLY);
    if (!fh) return -1;

    size_t elf_len;
    uint8_t *elf_data = read_all_file(fh, &elf_len);
    file_close(fh);
    if (!elf_data) return -1;

    /* 创建新地址空间 */
    address_space_t *new_as = as_create();
    if (!new_as) { heap_free(elf_data, elf_len); return -1; }

    map_kernel_to_user(new_as);
    uintptr_t entry = elf_load(new_as, elf_data, elf_len);
    heap_free(elf_data, elf_len);
    if (!entry) { as_destroy(new_as); return -1; }

    uintptr_t stack_vpn_end = va_vpn(USER_STACK_TOP);
    uintptr_t stack_vpn_start = stack_vpn_end - (USER_STACK_SIZE / PAGE_SIZE);
    as_map(new_as, stack_vpn_start, stack_vpn_end, NULL, 0, 0, PTE_V | PTE_R | PTE_W | PTE_U);

    proc->as = new_as;
    signal_clear(&proc->signal);

    thread_t *t = current_thread();
    t->ctx.ctx = context_user(entry);
    t->ctx.satp = make_satp(as_root_ppn(new_as));
    ctx_set_sp(&t->ctx.ctx, USER_STACK_TOP);

    return 0;
}

/* waitpid 返回值：
 * >= 0: 子进程 pid（已退出）
 * -1: 无匹配的子进程
 * -2: 有子进程但未退出，需要阻塞
 */
static long do_waitpid(long pid, int *exit_code) {
    process_t *proc = current_process();
    thread_t *t = current_thread();
    if (!proc || !t) return -1;

    int *kcode = NULL;
    if (exit_code) {
        kcode = as_translate(proc->as, (vaddr_t)exit_code, PTE_W | PTE_V);
    }

    bool has_child = false;

    /* 查找子进程 */
    for (pid_t i = 0; i < MAX_PROCS; i++) {
        process_t *child = &g_process_pool[i];
        if (child->parent == proc->pid) {
            if (pid == -1 || (pid_t)pid == i) {
                if (child->exited) {
                    /* 找到已退出的子进程 */
                    if (kcode) *kcode = child->exit_code;
                    child->parent = PID_INVALID;  /* 释放 */
                    return i;
                }
                has_child = true;
            }
        }
    }

    if (has_child) {
        /* 有子进程但未退出，记录等待状态 */
        proc->waiting_for = (pid_t)pid;
        proc->waiting_tid = t->tid;
        proc->waiting_exit_code_ptr = kcode;
        return -2;  /* 需要阻塞 */
    }

    return -1;  /* 无子进程 */
}

/* 信号系统调用 */
static long do_kill(int pid, int signum) {
    if (signum <= 0 || signum > MAX_SIG) return -1;
    process_t *target = get_process((pid_t)pid);
    if (!target) return -1;
    signal_add(&target->signal, (signal_no_t)signum);
    return 0;
}

static long do_sigaction(int signum, const void *action, void *old_action) {
    if (signum <= 0 || signum > MAX_SIG) return -1;
    process_t *proc = current_process();
    if (!proc) return -1;

    if (old_action) {
        signal_action_t *kold = as_translate(proc->as, (vaddr_t)old_action, PTE_W | PTE_V);
        if (!kold || !signal_get_action(&proc->signal, (signal_no_t)signum, kold)) return -1;
    }
    if (action) {
        const signal_action_t *kact = as_translate(proc->as, (vaddr_t)action, PTE_R | PTE_V);
        if (!kact || !signal_set_action(&proc->signal, (signal_no_t)signum, kact)) return -1;
    }
    return 0;
}

static long do_sigprocmask(uintptr_t mask) {
    process_t *proc = current_process();
    return proc ? signal_update_mask(&proc->signal, mask) : -1;
}

static long do_sigreturn(void) {
    process_t *proc = current_process();
    thread_t *t = current_thread();
    if (!proc || !t) return -1;
    return signal_return(&proc->signal, &t->ctx.ctx) ? 0 : -1;
}

/* 线程系统调用 */
static long do_thread_create(uintptr_t entry, uintptr_t arg) {
    process_t *proc = current_process();
    if (!proc || proc->thread_count >= MAX_THREADS) return -1;

    /* 为新线程分配栈 */
    uintptr_t stack_base = USER_STACK_TOP - (proc->thread_count + 1) * 3 * PAGE_SIZE;
    uintptr_t stack_vpn_start = va_vpn(stack_base);
    uintptr_t stack_vpn_end = stack_vpn_start + 2;
    as_map(proc->as, stack_vpn_start, stack_vpn_end, NULL, 0, 0, PTE_V | PTE_R | PTE_W | PTE_U);

    uintptr_t satp = make_satp(as_root_ppn(proc->as));
    thread_t *t = create_thread(proc->pid, entry, stack_base + 2 * PAGE_SIZE, satp);
    if (!t) return -1;

    ctx_set_arg(&t->ctx.ctx, 0, arg);

    proc->threads[proc->thread_count++] = t->tid;
    ready_enqueue(t->tid);

    return t->tid;
}

static long do_gettid(void) {
    thread_t *t = current_thread();
    return t ? t->tid : -1;
}

static long do_waittid(int tid) {
    thread_t *target = get_thread(tid);
    if (!target || target->pid != current_process()->pid) return -1;
    if (target->exited) return target->exit_code;
    return -1;  /* 简化：不阻塞 */
}

/* 同步原语系统调用 */
static long do_mutex_create(int blocking) {
    process_t *proc = current_process();
    if (!proc || !blocking) return -1;  /* 只支持 blocking mutex */

    for (int i = 0; i < MAX_SYNC_OBJS; i++) {
        if (!proc->mutexes[i]) {
            proc->mutexes[i] = heap_alloc(sizeof(mutex_t), 8);
            mutex_init(proc->mutexes[i]);
            return i;
        }
    }
    return -1;
}

static long do_mutex_lock(int mutex_id) {
    process_t *proc = current_process();
    thread_t *t = current_thread();
    if (!proc || mutex_id < 0 || mutex_id >= MAX_SYNC_OBJS || !proc->mutexes[mutex_id]) return -1;

    if (mutex_lock(proc->mutexes[mutex_id], t->tid)) {
        return 0;  /* 成功获取 */
    }
    return -1;  /* 需要阻塞 */
}

static long do_mutex_unlock(int mutex_id) {
    process_t *proc = current_process();
    if (!proc || mutex_id < 0 || mutex_id >= MAX_SYNC_OBJS || !proc->mutexes[mutex_id]) return -1;

    tid_t waking = mutex_unlock(proc->mutexes[mutex_id]);
    if (waking != TID_INVALID) ready_enqueue(waking);
    return 0;
}

static long do_semaphore_create(int res_count) {
    process_t *proc = current_process();
    if (!proc) return -1;

    for (int i = 0; i < MAX_SYNC_OBJS; i++) {
        if (!proc->semaphores[i]) {
            proc->semaphores[i] = heap_alloc(sizeof(semaphore_t), 8);
            sem_init(proc->semaphores[i], res_count);
            return i;
        }
    }
    return -1;
}

static long do_semaphore_up(int sem_id) {
    process_t *proc = current_process();
    if (!proc || sem_id < 0 || sem_id >= MAX_SYNC_OBJS || !proc->semaphores[sem_id]) return -1;

    tid_t waking = sem_up(proc->semaphores[sem_id]);
    if (waking != TID_INVALID) ready_enqueue(waking);
    return 0;
}

static long do_semaphore_down(int sem_id) {
    process_t *proc = current_process();
    thread_t *t = current_thread();
    if (!proc || sem_id < 0 || sem_id >= MAX_SYNC_OBJS || !proc->semaphores[sem_id]) return -1;

    if (sem_down(proc->semaphores[sem_id], t->tid)) {
        return 0;
    }
    return -1;  /* 需要阻塞 */
}

static long do_condvar_create(int arg) {
    (void)arg;
    process_t *proc = current_process();
    if (!proc) return -1;

    for (int i = 0; i < MAX_SYNC_OBJS; i++) {
        if (!proc->condvars[i]) {
            proc->condvars[i] = heap_alloc(sizeof(condvar_t), 8);
            condvar_init(proc->condvars[i]);
            return i;
        }
    }
    return -1;
}

static long do_condvar_signal(int condvar_id) {
    process_t *proc = current_process();
    if (!proc || condvar_id < 0 || condvar_id >= MAX_SYNC_OBJS || !proc->condvars[condvar_id]) return -1;

    tid_t waking = condvar_signal(proc->condvars[condvar_id]);
    if (waking != TID_INVALID) ready_enqueue(waking);
    return 0;
}

static long do_condvar_wait(int condvar_id, int mutex_id) {
    process_t *proc = current_process();
    thread_t *t = current_thread();
    if (!proc || condvar_id < 0 || condvar_id >= MAX_SYNC_OBJS || !proc->condvars[condvar_id]) return -1;
    if (mutex_id < 0 || mutex_id >= MAX_SYNC_OBJS || !proc->mutexes[mutex_id]) return -1;

    condvar_wait_result_t r = condvar_wait_with_mutex(proc->condvars[condvar_id],
                                                       proc->mutexes[mutex_id], t->tid);
    if (r.waking_tid != TID_INVALID) ready_enqueue(r.waking_tid);
    return r.need_block ? -1 : 0;
}

/* 接口注册 */
static syscall_io_t io_impl;
static syscall_proc_t proc_impl;
static syscall_sched_t sched_impl;
static syscall_clock_t clock_impl;
static syscall_signal_t signal_impl_s;
static syscall_thread_t thread_impl;
static syscall_sync_t sync_impl;

static void init_syscall(void) {
    io_impl.write = do_write;
    io_impl.read = do_read;
    io_impl.open = do_open;
    io_impl.close = do_close;

    proc_impl.exit = do_exit;
    proc_impl.fork = do_fork;
    proc_impl.exec = do_exec;
    proc_impl.waitpid = do_waitpid;
    proc_impl.getpid = do_getpid;

    sched_impl.sched_yield = do_sched_yield;
    clock_impl.clock_gettime = do_clock_gettime;

    signal_impl_s.kill = do_kill;
    signal_impl_s.sigaction = do_sigaction;
    signal_impl_s.sigprocmask = do_sigprocmask;
    signal_impl_s.sigreturn = do_sigreturn;

    thread_impl.thread_create = do_thread_create;
    thread_impl.gettid = do_gettid;
    thread_impl.waittid = do_waittid;

    sync_impl.mutex_create = do_mutex_create;
    sync_impl.mutex_lock = do_mutex_lock;
    sync_impl.mutex_unlock = do_mutex_unlock;
    sync_impl.semaphore_create = do_semaphore_create;
    sync_impl.semaphore_up = do_semaphore_up;
    sync_impl.semaphore_down = do_semaphore_down;
    sync_impl.condvar_create = do_condvar_create;
    sync_impl.condvar_signal = do_condvar_signal;
    sync_impl.condvar_wait = do_condvar_wait;

    syscall_set_io(&io_impl);
    syscall_set_proc(&proc_impl);
    syscall_set_sched(&sched_impl);
    syscall_set_clock(&clock_impl);
    syscall_set_signal(&signal_impl_s);
    syscall_set_thread(&thread_impl);
    syscall_set_sync(&sync_impl);
}

/* ============================================================================
 * 主函数
 * ========================================================================== */

static const char *exception_name(uintptr_t code) {
    static const char *names[] = {
        [0] = "InstructionMisaligned", [1] = "InstructionFault",
        [2] = "IllegalInstruction", [3] = "Breakpoint",
        [4] = "LoadMisaligned", [5] = "LoadFault",
        [6] = "StoreMisaligned", [7] = "StoreFault",
        [8] = "UserEnvCall", [12] = "InstructionPageFault",
        [13] = "LoadPageFault", [15] = "StorePageFault",
    };
    if (code < sizeof(names)/sizeof(names[0]) && names[code]) return names[code];
    return "Unknown";
}

void main(void) {
    kernel_layout_t layout = kernel_layout();
    clear_bss(&layout);
    g_layout = layout;
    puts("");

    uintptr_t heap_start = g_layout.end;
    g_memory_end = g_layout.text + MEMORY_SIZE;
    heap_init(heap_start, g_memory_end - heap_start);
    printf("[INFO] heap: %p - %p\n", (void *)heap_start, (void *)g_memory_end);

    block_cache_init();
    if (virtio_blk_init(&g_virtio_blk) != 0) { puts("[PANIC] virtio init failed!"); shutdown(); }
    g_block_dev = virtio_blk_as_block_device(&g_virtio_blk);
    puts("[INFO] virtio block device initialized");

    g_fs = efs_open(g_block_dev);
    if (!g_fs) { puts("[PANIC] failed to open easy-fs!"); shutdown(); }
    g_root = efs_root_inode(g_fs);
    puts("[INFO] easy-fs mounted");

    kernel_as = as_create();
    map_kernel_to_user(kernel_as);
    init_syscall();

    /* 加载 initproc */
    file_handle_t *initproc_fh = file_open(g_fs, "initproc", O_RDONLY);
    if (!initproc_fh) { puts("[PANIC] initproc not found!"); shutdown(); }
    size_t initproc_len;
    uint8_t *initproc_data = read_all_file(initproc_fh, &initproc_len);
    file_close(initproc_fh);

    process_t *init_proc;
    thread_t *init_thread;
    if (!create_process_from_elf(initproc_data, initproc_len, &init_proc, &init_thread)) {
        puts("[PANIC] failed to create initproc!");
        shutdown();
    }
    heap_free(initproc_data, initproc_len);

    ready_enqueue(init_thread->tid);
    printf("[INFO] initproc created, pid=%d, tid=%d\n", (int)init_proc->pid, (int)init_thread->tid);
    puts("");

    write_satp(make_satp(as_root_ppn(kernel_as)));
    puts("[INFO] paging enabled\n");

    /* 调度循环 */
    while (1) {
        tid_t tid = ready_dequeue();
        if (tid == TID_INVALID) { puts("no task"); break; }

        thread_t *t = get_thread(tid);
        if (!t || t->exited) continue;

        g_current_tid = tid;
        foreign_ctx_run(&t->ctx);

        uintptr_t scause = read_scause();
        uintptr_t code = cause_code(scause);

        if (is_exception(scause) && code == EXCEP_U_ECALL) {
            context_t *ctx = &t->ctx.ctx;
            ctx_move_next(ctx);

            uintptr_t args[6];
            for (int i = 0; i < 6; i++) args[i] = ctx_arg(ctx, i);
            uintptr_t id = ctx_arg(ctx, 7);

            syscall_result_t ret = syscall_dispatch(id, args);

            /* 处理信号 */
            process_t *proc = get_process(t->pid);
            signal_result_t sig_ret = signal_handle(&proc->signal, ctx);
            if (sig_ret.type == SIGNAL_PROCESS_KILLED) {
                proc->exited = true;
                proc->exit_code = sig_ret.exit_code;
                t->exited = true;
                continue;
            }

            if (id == SYS_EXIT) {
                t->exit_code = (int)args[0];
                t->exited = true;
                /* 检查是否所有线程都退出 */
                bool all_exited = true;
                for (int i = 0; i < proc->thread_count; i++) {
                    thread_t *pt = get_thread(proc->threads[i]);
                    if (pt && !pt->exited) { all_exited = false; break; }
                }
                if (all_exited) {
                    proc->exited = true;
                    proc->exit_code = t->exit_code;
                    /* 唤醒等待的父进程 */
                    if (proc->parent != PID_INVALID) {
                        process_t *parent = get_process(proc->parent);
                        if (parent && parent->waiting_tid != TID_INVALID) {
                            if (parent->waiting_for == (pid_t)-1 || parent->waiting_for == proc->pid) {
                                /* 设置父进程线程的返回值 */
                                thread_t *pt = get_thread(parent->waiting_tid);
                                if (pt) {
                                    ctx_set_arg(&pt->ctx.ctx, 0, proc->pid);
                                    /* 写入 exit_code */
                                    if (parent->waiting_exit_code_ptr) {
                                        *(parent->waiting_exit_code_ptr) = proc->exit_code;
                                    }
                                }
                                ready_enqueue(parent->waiting_tid);
                                parent->waiting_tid = TID_INVALID;
                                parent->waiting_for = PID_INVALID;
                                parent->waiting_exit_code_ptr = NULL;
                                proc->parent = PID_INVALID;  /* 释放 */
                            }
                        }
                    }
                }
            } else if (id == SYS_WAITPID) {
                if (ret.value == -2) {
                    /* 需要阻塞等待子进程 */
                    /* 不设置返回值，不入队 */
                } else {
                    ctx_set_arg(ctx, 0, ret.value);
                    ready_enqueue(tid);
                }
            } else if (ret.status == SYSCALL_OK) {
                /* 检查是否需要阻塞 */
                bool need_block = (ret.value == -1) &&
                    (id == SYS_MUTEX_LOCK || id == SYS_SEMAPHORE_DOWN || id == SYS_CONDVAR_WAIT);

                ctx_set_arg(ctx, 0, ret.value);

                if (!need_block) {
                    ready_enqueue(tid);
                }
                /* 阻塞的线程不入队 */
            } else {
                printf("[ERROR] tid=%d unsupported syscall %d\n", (int)tid, (int)id);
                t->exited = true;
            }
        } else if (is_exception(scause)) {
            printf("[ERROR] tid=%d killed: %s\n", (int)tid, exception_name(code));
            t->exited = true;
        } else {
            printf("[ERROR] tid=%d killed: unexpected interrupt\n", (int)tid);
            t->exited = true;
        }

        g_current_tid = TID_INVALID;
    }

    shutdown();
}
