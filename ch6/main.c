/**
 * ch6 - 文件系统
 *
 * 支持 easy-fs 文件系统和 VirtIO 块设备
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
#include "../task-manage/proc_manage.h"
#include "../util/printf.h"
#include "../util/riscv.h"
#include "../util/sbi.h"
#include "../easy-fs/easy_fs.h"
#include "../virtio-block/virtio_block.h"

/* ============================================================================
 * 配置
 * ========================================================================== */

#define MEMORY_SIZE     (48 << 20)      /* 48 MB */
#define USER_STACK_SIZE (2 * PAGE_SIZE)
#define USER_STACK_TOP  (1UL << 38)
#define MAX_FD          16

/* MMIO 区域 */
#define VIRTIO_MMIO_BASE 0x10001000
#define VIRTIO_MMIO_SIZE 0x1000

/* ============================================================================
 * 全局状态
 * ========================================================================== */

static kernel_layout_t g_layout;
static uintptr_t g_memory_end;

static address_space_t *kernel_as;

static proc_manager_t g_pm;

/* VirtIO 和文件系统 */
static virtio_blk_t g_virtio_blk;
static block_device_t *g_block_dev;
static easy_fs_t *g_fs;
static inode_t *g_root;

/* ============================================================================
 * 进程结构（带文件描述符表）
 * ========================================================================== */

struct process {
    pid_t pid;
    foreign_ctx_t ctx;
    address_space_t *as;
    /* 文件描述符表 */
    file_handle_t *fd_table[MAX_FD];
};

static struct process g_process_pool[MAX_PROCS];

/* ============================================================================
 * 辅助函数
 * ========================================================================== */

static void map_kernel_to_user(address_space_t *user_as) {
    uintptr_t text_start = g_layout.text;
    uintptr_t text_end = g_layout.rodata;
    as_map_extern(user_as, va_vpn(text_start), va_vpn(text_end),
                  pa_ppn(text_start), PTE_V | PTE_R | PTE_X);

    as_map_extern(user_as, va_vpn(g_layout.rodata), va_vpn(g_layout.data),
                  pa_ppn(g_layout.rodata), PTE_V | PTE_R);

    as_map_extern(user_as, va_vpn(g_layout.data), va_vpn(g_memory_end),
                  pa_ppn(g_layout.data), PTE_V | PTE_R | PTE_W);

    /* 映射 MMIO */
    as_map_extern(user_as, va_vpn(VIRTIO_MMIO_BASE),
                  va_vpn(VIRTIO_MMIO_BASE + VIRTIO_MMIO_SIZE),
                  pa_ppn(VIRTIO_MMIO_BASE), PTE_V | PTE_R | PTE_W);
}

/* 从文件系统读取整个文件 */
static uint8_t *read_all(file_handle_t *fh, size_t *out_len) {
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
 * 进程操作
 * ========================================================================== */

static struct process *create_process_from_elf(const uint8_t *elf_data, size_t elf_len) {
    pid_t pid = pid_alloc();
    if (pid >= MAX_PROCS) return NULL;

    struct process *proc = &g_process_pool[pid];
    proc->pid = pid;

    proc->as = as_create();
    if (!proc->as) return NULL;

    map_kernel_to_user(proc->as);

    uintptr_t entry = elf_load(proc->as, elf_data, elf_len);
    if (!entry) {
        as_destroy(proc->as);
        return NULL;
    }

    uintptr_t stack_vpn_end = va_vpn(USER_STACK_TOP);
    uintptr_t stack_vpn_start = stack_vpn_end - (USER_STACK_SIZE / PAGE_SIZE);
    as_map(proc->as, stack_vpn_start, stack_vpn_end, NULL, 0, 0,
           PTE_V | PTE_R | PTE_W | PTE_U);

    proc->ctx.ctx = context_user(entry);
    proc->ctx.satp = make_satp(as_root_ppn(proc->as));
    ctx_set_sp(&proc->ctx.ctx, USER_STACK_TOP);

    /* 初始化文件描述符表 */
    memset(proc->fd_table, 0, sizeof(proc->fd_table));
    /* fd 0: stdin (空，由 read 特殊处理) */
    proc->fd_table[0] = heap_alloc(sizeof(file_handle_t), 8);
    proc->fd_table[0]->inode = NULL;
    proc->fd_table[0]->readable = true;
    proc->fd_table[0]->writable = false;
    /* fd 1: stdout */
    proc->fd_table[1] = heap_alloc(sizeof(file_handle_t), 8);
    proc->fd_table[1]->inode = NULL;
    proc->fd_table[1]->readable = false;
    proc->fd_table[1]->writable = true;

    return proc;
}

static struct process *fork_process(struct process *parent) {
    pid_t pid = pid_alloc();
    if (pid >= MAX_PROCS) return NULL;

    struct process *child = &g_process_pool[pid];
    child->pid = pid;

    child->as = as_clone(parent->as);
    if (!child->as) return NULL;

    child->ctx.ctx = parent->ctx.ctx;
    child->ctx.satp = make_satp(as_root_ppn(child->as));

    /* 复制文件描述符表 */
    for (int i = 0; i < MAX_FD; i++) {
        if (parent->fd_table[i]) {
            child->fd_table[i] = heap_alloc(sizeof(file_handle_t), 8);
            memcpy(child->fd_table[i], parent->fd_table[i], sizeof(file_handle_t));
            /* 复制 inode */
            if (parent->fd_table[i]->inode) {
                child->fd_table[i]->inode = heap_alloc(sizeof(inode_t), 8);
                memcpy(child->fd_table[i]->inode, parent->fd_table[i]->inode, sizeof(inode_t));
            }
        } else {
            child->fd_table[i] = NULL;
        }
    }

    return child;
}

static int exec_process(struct process *proc, const uint8_t *elf_data, size_t elf_len) {
    address_space_t *new_as = as_create();
    if (!new_as) return -1;

    map_kernel_to_user(new_as);

    uintptr_t entry = elf_load(new_as, elf_data, elf_len);
    if (!entry) {
        as_destroy(new_as);
        return -1;
    }

    uintptr_t stack_vpn_end = va_vpn(USER_STACK_TOP);
    uintptr_t stack_vpn_start = stack_vpn_end - (USER_STACK_SIZE / PAGE_SIZE);
    as_map(new_as, stack_vpn_start, stack_vpn_end, NULL, 0, 0,
           PTE_V | PTE_R | PTE_W | PTE_U);

    proc->as = new_as;
    proc->ctx.ctx = context_user(entry);
    proc->ctx.satp = make_satp(as_root_ppn(proc->as));
    ctx_set_sp(&proc->ctx.ctx, USER_STACK_TOP);

    return 0;
}

/* ============================================================================
 * 系统调用实现
 * ========================================================================== */

static long do_open(const char *path, uint32_t flags) {
    struct process *proc = pm_current(&g_pm);
    if (!proc) return -1;

    /* 翻译路径 */
    const char *kpath = as_translate(proc->as, (vaddr_t)path, PTE_R | PTE_V);
    if (!kpath) return -1;

    /* 找空闲 fd */
    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!proc->fd_table[i]) {
            fd = i;
            break;
        }
    }
    if (fd < 0) return -1;

    /* 打开文件 */
    file_handle_t *fh = file_open(g_fs, kpath, flags);
    if (!fh) return -1;

    proc->fd_table[fd] = fh;
    return fd;
}

static long do_close(int fd) {
    struct process *proc = pm_current(&g_pm);
    if (!proc || fd < 0 || fd >= MAX_FD) return -1;

    if (proc->fd_table[fd]) {
        file_close(proc->fd_table[fd]);
        proc->fd_table[fd] = NULL;
        return 0;
    }
    return -1;
}

static long do_write(int fd, const void *buf, size_t count) {
    struct process *proc = pm_current(&g_pm);
    if (!proc) return -1;

    const char *kbuf = as_translate(proc->as, (vaddr_t)buf, PTE_R | PTE_V);
    if (!kbuf) return -1;

    if (fd == FD_STDOUT || fd == FD_STDERR) {
        for (size_t i = 0; i < count; i++) {
            console_putchar(kbuf[i]);
        }
        return count;
    }

    if (fd < 0 || fd >= MAX_FD || !proc->fd_table[fd]) return -1;

    file_handle_t *fh = proc->fd_table[fd];
    if (!fh->writable) return -1;

    return file_write(fh, (const uint8_t *)kbuf, count);
}

static long do_read(int fd, void *buf, size_t count) {
    struct process *proc = pm_current(&g_pm);
    if (!proc) return -1;

    char *kbuf = as_translate(proc->as, (vaddr_t)buf, PTE_W | PTE_V);
    if (!kbuf) return -1;

    if (fd == FD_STDIN) {
        for (size_t i = 0; i < count; i++) {
            kbuf[i] = console_getchar();
        }
        return count;
    }

    if (fd < 0 || fd >= MAX_FD || !proc->fd_table[fd]) return -1;

    file_handle_t *fh = proc->fd_table[fd];
    if (!fh->readable) return -1;

    return file_read(fh, (uint8_t *)kbuf, count);
}

static void do_exit(int code) {
    (void)code;
}

static long do_sched_yield(void) {
    return 0;
}

static long do_clock_gettime(int clock_id, timespec_t *tp) {
    if (clock_id == CLOCK_MONOTONIC && tp) {
        struct process *proc = pm_current(&g_pm);
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

static long do_getpid(void) {
    return pm_current_pid(&g_pm);
}

static long do_fork(void) {
    struct process *parent = pm_current(&g_pm);
    if (!parent) return -1;

    struct process *child = fork_process(parent);
    if (!child) return -1;

    ctx_set_arg(&child->ctx.ctx, 0, 0);
    pm_add(&g_pm, child->pid, child, parent->pid);
    return child->pid;
}

static long do_exec(const char *path, size_t len) {
    struct process *proc = pm_current(&g_pm);
    if (!proc) return -1;

    const char *kpath = as_translate(proc->as, (vaddr_t)path, PTE_R | PTE_V);
    if (!kpath) return -1;

    /* 从文件系统读取 */
    char name[32];
    if (len > 31) len = 31;
    memcpy(name, kpath, len);
    name[len] = '\0';

    file_handle_t *fh = file_open(g_fs, name, O_RDONLY);
    if (!fh) {
        printf("[ERROR] exec: file not found: %s\n", name);
        return -1;
    }

    size_t elf_len;
    uint8_t *elf_data = read_all(fh, &elf_len);
    file_close(fh);

    if (!elf_data) return -1;

    int ret = exec_process(proc, elf_data, elf_len);
    heap_free(elf_data, elf_len);
    return ret;
}

static long do_waitpid(long pid, int *exit_code) {
    struct process *proc = pm_current(&g_pm);
    if (!proc) return -1;

    int *kcode = NULL;
    if (exit_code) {
        kcode = as_translate(proc->as, (vaddr_t)exit_code, PTE_W | PTE_V);
    }

    wait_result_t result = pm_wait(&g_pm, (pid_t)pid);
    if (!result.found) return -1;

    if (kcode) *kcode = result.exit_code;
    return result.pid;
}

static syscall_io_t io_impl;
static syscall_proc_t proc_impl;
static syscall_sched_t sched_impl;
static syscall_clock_t clock_impl;

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

    syscall_set_io(&io_impl);
    syscall_set_proc(&proc_impl);
    syscall_set_sched(&sched_impl);
    syscall_set_clock(&clock_impl);
}

/* ============================================================================
 * 异常名称
 * ========================================================================== */

static const char *exception_name(uintptr_t code) {
    static const char *names[] = {
        [0]  = "InstructionMisaligned",
        [1]  = "InstructionFault",
        [2]  = "IllegalInstruction",
        [3]  = "Breakpoint",
        [4]  = "LoadMisaligned",
        [5]  = "LoadFault",
        [6]  = "StoreMisaligned",
        [7]  = "StoreFault",
        [8]  = "UserEnvCall",
        [9]  = "SupervisorEnvCall",
        [12] = "InstructionPageFault",
        [13] = "LoadPageFault",
        [15] = "StorePageFault",
    };
    if (code < sizeof(names) / sizeof(names[0]) && names[code]) {
        return names[code];
    }
    return "Unknown";
}

/* ============================================================================
 * 主函数
 * ========================================================================== */

void main(void) {
    kernel_layout_t layout = kernel_layout();
    clear_bss(&layout);
    g_layout = layout;

    puts("");

    /* 初始化堆 */
    uintptr_t heap_start = g_layout.end;
    g_memory_end = g_layout.text + MEMORY_SIZE;
    size_t heap_size = g_memory_end - heap_start;
    heap_init(heap_start, heap_size);
    printf("[INFO] heap: %p - %p (%d KB)\n",
           (void *)heap_start, (void *)g_memory_end, (int)(heap_size / 1024));

    /* 初始化块缓存 */
    block_cache_init();

    /* 初始化 VirtIO 块设备 */
    if (virtio_blk_init(&g_virtio_blk) != 0) {
        puts("[PANIC] virtio block init failed!");
        shutdown();
    }
    g_block_dev = virtio_blk_as_block_device(&g_virtio_blk);
    puts("[INFO] virtio block device initialized");

    /* 打开文件系统 */
    puts("[INFO] opening easy-fs...");
    g_fs = efs_open(g_block_dev);
    if (!g_fs) {
        puts("[PANIC] failed to open easy-fs!");
        shutdown();
    }
    puts("[INFO] getting root inode...");
    g_root = efs_root_inode(g_fs);
    puts("[INFO] easy-fs mounted");

    /* 列出文件 */
    char names[16][NAME_LENGTH_LIMIT + 1];
    size_t count = inode_readdir(g_root, names, 16);
    printf("[INFO] files in root: ");
    for (size_t i = 0; i < count; i++) {
        printf("%s ", names[i]);
    }
    puts("");

    /* 创建内核地址空间 */
    kernel_as = as_create();
    map_kernel_to_user(kernel_as);
    printf("[INFO] kernel space created\n");

    /* 初始化进程管理器 */
    pm_init(&g_pm);

    /* 初始化系统调用 */
    init_syscall();

    /* 从文件系统加载 initproc */
    file_handle_t *initproc_fh = file_open(g_fs, "initproc", O_RDONLY);
    if (!initproc_fh) {
        puts("[PANIC] initproc not found in fs!");
        shutdown();
    }

    size_t initproc_len;
    uint8_t *initproc_data = read_all(initproc_fh, &initproc_len);
    file_close(initproc_fh);

    if (!initproc_data) {
        puts("[PANIC] failed to read initproc!");
        shutdown();
    }

    struct process *init = create_process_from_elf(initproc_data, initproc_len);
    heap_free(initproc_data, initproc_len);

    if (!init) {
        puts("[PANIC] failed to create initproc!");
        shutdown();
    }
    pm_add(&g_pm, init->pid, init, PID_INVALID);
    printf("[INFO] initproc created, pid=%d\n", (int)init->pid);

    puts("");

    /* 启用分页 */
    write_satp(make_satp(as_root_ppn(kernel_as)));
    puts("[INFO] paging enabled\n");

    /* 调度循环 */
    while (1) {
        struct process *proc = pm_find_next(&g_pm);
        if (!proc) {
            puts("no task");
            break;
        }

        foreign_ctx_run(&proc->ctx);

        uintptr_t scause = read_scause();
        uintptr_t code = cause_code(scause);

        if (is_exception(scause) && code == EXCEP_U_ECALL) {
            context_t *ctx = &proc->ctx.ctx;
            ctx_move_next(ctx);

            uintptr_t args[6];
            for (int i = 0; i < 6; i++) {
                args[i] = ctx_arg(ctx, i);
            }
            uintptr_t id = ctx_arg(ctx, 7);

            syscall_result_t ret = syscall_dispatch(id, args);

            if (id == SYS_EXIT) {
                pm_exit_current(&g_pm, (int)args[0]);
            } else if (ret.status == SYSCALL_OK) {
                ctx_set_arg(ctx, 0, ret.value);
                pm_suspend_current(&g_pm);
            } else {
                printf("[ERROR] pid=%d unsupported syscall %d\n",
                       (int)proc->pid, (int)id);
                pm_exit_current(&g_pm, -2);
            }
        } else if (is_exception(scause)) {
            printf("[ERROR] pid=%d killed: %s, stval=%p, sepc=%p\n",
                   (int)proc->pid, exception_name(code),
                   (void *)read_stval(), (void *)ctx_pc(&proc->ctx.ctx));
            pm_exit_current(&g_pm, -3);
        } else {
            printf("[ERROR] pid=%d killed: unexpected interrupt %d\n",
                   (int)proc->pid, (int)code);
            pm_exit_current(&g_pm, -3);
        }
    }

    shutdown();
}
