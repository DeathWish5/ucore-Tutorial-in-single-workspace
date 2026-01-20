/**
 * ch5 - 进程管理
 *
 * 支持 fork, exec, wait, getpid 系统调用。
 * 从 initproc 启动，由 initproc fork 并 exec user_shell。
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

/* ============================================================================
 * 配置
 * ========================================================================== */

#define MEMORY_SIZE     (48 << 20)      /* 48 MB */
#define USER_STACK_SIZE (2 * PAGE_SIZE)
#define USER_STACK_TOP  (1UL << 38)

/* ============================================================================
 * 全局状态
 * ========================================================================== */

/* 内核布局 */
static kernel_layout_t g_layout;
static uintptr_t g_memory_end;

/* 内核地址空间 */
static address_space_t *kernel_as;

/* 进程管理器 */
static proc_manager_t g_pm;

/* 应用程序表 */
typedef struct {
    const char *name;
    const uint8_t *data;
    size_t len;
} app_entry_t;

#define MAX_APPS 32
static app_entry_t g_apps[MAX_APPS];
static size_t g_app_count = 0;

/* ============================================================================
 * 进程结构
 * ========================================================================== */

struct process {
    pid_t pid;
    foreign_ctx_t ctx;
    address_space_t *as;
};

static struct process g_process_pool[MAX_PROCS];

/* ============================================================================
 * 辅助函数
 * ========================================================================== */

static void map_kernel_to_user(address_space_t *user_as) {
    /* 映射内核代码段 */
    uintptr_t text_start = g_layout.text;
    uintptr_t text_end = g_layout.rodata;
    as_map_extern(user_as,
                  va_vpn(text_start), va_vpn(text_end),
                  pa_ppn(text_start), PTE_V | PTE_R | PTE_X);

    /* 映射内核只读数据 */
    as_map_extern(user_as,
                  va_vpn(g_layout.rodata), va_vpn(g_layout.data),
                  pa_ppn(g_layout.rodata), PTE_V | PTE_R);

    /* 映射内核数据和堆 */
    as_map_extern(user_as,
                  va_vpn(g_layout.data), va_vpn(g_memory_end),
                  pa_ppn(g_layout.data), PTE_V | PTE_R | PTE_W);
}

static const app_entry_t *find_app(const char *name, size_t len) {
    for (size_t i = 0; i < g_app_count; i++) {
        if (strlen(g_apps[i].name) == len &&
            memcmp(g_apps[i].name, name, len) == 0) {
            return &g_apps[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * 进程操作
 * ========================================================================== */

static struct process *create_process_from_elf(const uint8_t *elf_data, size_t elf_len) {
    /* 分配进程 */
    pid_t pid = pid_alloc();
    if (pid >= MAX_PROCS) return NULL;

    struct process *proc = &g_process_pool[pid];
    proc->pid = pid;

    /* 创建地址空间 */
    proc->as = as_create();
    if (!proc->as) return NULL;

    /* 映射内核空间 */
    map_kernel_to_user(proc->as);

    /* 加载 ELF */
    uintptr_t entry = elf_load(proc->as, elf_data, elf_len);
    if (!entry) {
        as_destroy(proc->as);
        return NULL;
    }

    /* 分配用户栈 */
    uintptr_t stack_vpn_end = va_vpn(USER_STACK_TOP);
    uintptr_t stack_vpn_start = stack_vpn_end - (USER_STACK_SIZE / PAGE_SIZE);
    as_map(proc->as, stack_vpn_start, stack_vpn_end, NULL, 0, 0,
           PTE_V | PTE_R | PTE_W | PTE_U);

    /* 初始化上下文 */
    proc->ctx.ctx = context_user(entry);
    proc->ctx.satp = make_satp(as_root_ppn(proc->as));
    ctx_set_sp(&proc->ctx.ctx, USER_STACK_TOP);

    return proc;
}

static struct process *fork_process(struct process *parent) {
    /* 分配进程 */
    pid_t pid = pid_alloc();
    if (pid >= MAX_PROCS) return NULL;

    struct process *child = &g_process_pool[pid];
    child->pid = pid;

    /* 复制地址空间 */
    child->as = as_clone(parent->as);
    if (!child->as) return NULL;

    /* 复制上下文 */
    child->ctx.ctx = parent->ctx.ctx;
    child->ctx.satp = make_satp(as_root_ppn(child->as));

    return child;
}

static int exec_process(struct process *proc, const uint8_t *elf_data, size_t elf_len) {
    /* 创建新地址空间 */
    address_space_t *new_as = as_create();
    if (!new_as) return -1;

    /* 映射内核空间 */
    map_kernel_to_user(new_as);

    /* 加载 ELF */
    uintptr_t entry = elf_load(new_as, elf_data, elf_len);
    if (!entry) {
        as_destroy(new_as);
        return -1;
    }

    /* 分配用户栈 */
    uintptr_t stack_vpn_end = va_vpn(USER_STACK_TOP);
    uintptr_t stack_vpn_start = stack_vpn_end - (USER_STACK_SIZE / PAGE_SIZE);
    as_map(new_as, stack_vpn_start, stack_vpn_end, NULL, 0, 0,
           PTE_V | PTE_R | PTE_W | PTE_U);

    /* 替换地址空间 */
    /* TODO: 释放旧地址空间 */
    proc->as = new_as;

    /* 重置上下文 */
    proc->ctx.ctx = context_user(entry);
    proc->ctx.satp = make_satp(as_root_ppn(proc->as));
    ctx_set_sp(&proc->ctx.ctx, USER_STACK_TOP);

    return 0;
}

/* ============================================================================
 * 系统调用实现
 * ========================================================================== */

static long do_write(int fd, const void *buf, size_t count) {
    if (fd == FD_STDOUT || fd == FD_STDERR) {
        struct process *proc = pm_current(&g_pm);
        if (!proc) return -1;

        const char *s = as_translate(proc->as, (vaddr_t)buf, PTE_R | PTE_V);
        if (!s) return -1;

        for (size_t i = 0; i < count; i++) {
            console_putchar(s[i]);
        }
        return count;
    }
    return -1;
}

static long do_read(int fd, void *buf, size_t count) {
    if (fd == FD_STDIN) {
        struct process *proc = pm_current(&g_pm);
        if (!proc) return -1;

        char *dst = as_translate(proc->as, (vaddr_t)buf, PTE_W | PTE_V);
        if (!dst) return -1;

        for (size_t i = 0; i < count; i++) {
            dst[i] = console_getchar();
        }
        return count;
    }
    return -1;
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

    /* 子进程返回 0 */
    ctx_set_arg(&child->ctx.ctx, 0, 0);

    /* 添加到进程管理器 */
    pm_add(&g_pm, child->pid, child, parent->pid);

    /* 父进程返回子进程 PID */
    return child->pid;
}

static long do_exec(const char *path, size_t len) {
    struct process *proc = pm_current(&g_pm);
    if (!proc) return -1;

    /* 翻译路径 */
    const char *kpath = as_translate(proc->as, (vaddr_t)path, PTE_R | PTE_V);
    if (!kpath) return -1;

    /* 查找应用 */
    const app_entry_t *app = find_app(kpath, len);
    if (!app) {
        printf("[ERROR] unknown app: %.*s\n", (int)len, kpath);
        return -1;
    }

    /* 执行 */
    return exec_process(proc, app->data, app->len);
}

static long do_waitpid(long pid, int *exit_code) {
    struct process *proc = pm_current(&g_pm);
    if (!proc) return -1;

    int *kcode = NULL;
    if (exit_code) {
        kcode = as_translate(proc->as, (vaddr_t)exit_code, PTE_W | PTE_V);
    }

    wait_result_t result = pm_wait(&g_pm, (pid_t)pid);
    if (!result.found) {
        return -1;
    }

    if (kcode) {
        *kcode = result.exit_code;
    }
    return result.pid;
}

static syscall_io_t io_impl;
static syscall_proc_t proc_impl;
static syscall_sched_t sched_impl;
static syscall_clock_t clock_impl;

static void init_syscall(void) {
    io_impl.write = do_write;
    io_impl.read = do_read;

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
 * 加载应用程序
 * ========================================================================== */

static void load_apps(void) {
    extern const char app_names[];
    extern const uint64_t app_count;

    const app_meta_t *meta = apps_meta();
    if (!meta) {
        puts("[WARN] no apps meta");
        return;
    }

    const char *name_ptr = app_names;
    app_iter_t iter = apps_iter(meta);
    size_t app_size;

    printf("[INFO] loading %d apps\n", (int)app_count);

    for (size_t i = 0; i < app_count && g_app_count < MAX_APPS; i++) {
        const uint8_t *app = apps_next(&iter, &app_size);
        if (!app) {
            puts("[WARN] apps_next returned NULL");
            break;
        }

        g_apps[g_app_count].name = name_ptr;
        g_apps[g_app_count].data = app;
        g_apps[g_app_count].len = app_size;

        printf("[INFO] app[%d] '%s' %p..%p\n",
               (int)g_app_count, name_ptr, (void *)app, (void *)(app + app_size));

        /* 移动到下一个名字 */
        while (*name_ptr) name_ptr++;
        name_ptr++;

        g_app_count++;
    }
    printf("[INFO] loaded %d apps\n", (int)g_app_count);
}

/* ============================================================================
 * 主函数
 * ========================================================================== */

void main(void) {
    /* 获取内核布局 */
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

    /* 创建内核地址空间 */
    kernel_as = as_create();
    map_kernel_to_user(kernel_as);
    printf("[INFO] kernel space created\n");

    /* 初始化进程管理器 */
    pm_init(&g_pm);

    /* 初始化系统调用 */
    init_syscall();

    /* 加载应用程序表 */
    load_apps();

    /* 查找 initproc */
    const app_entry_t *initproc = find_app("initproc", 8);
    if (!initproc) {
        puts("[PANIC] initproc not found!");
        shutdown();
    }

    /* 创建初始进程 */
    struct process *init = create_process_from_elf(initproc->data, initproc->len);
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
