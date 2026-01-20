/**
 * ch4 - 地址空间
 *
 * 每个进程拥有独立的虚拟地址空间。
 * 使用 Sv39 页表实现虚拟内存。
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

/* ============================================================================
 * 配置
 * ========================================================================== */

#define MEMORY_SIZE     (24 << 20)      /* 24 MB */
#define MAX_PROCESSES   16
#define USER_STACK_SIZE (2 * PAGE_SIZE)
#define USER_STACK_TOP  (1UL << 38)     /* 用户栈顶虚拟地址 */

/* ============================================================================
 * 进程结构
 * ========================================================================== */

typedef struct {
    foreign_ctx_t ctx;
    address_space_t *as;
    bool valid;
} process_t;

static process_t processes[MAX_PROCESSES];
static int process_count = 0;

/* 内核地址空间 */
static address_space_t *kernel_as;

/* ============================================================================
 * 系统调用实现
 * ========================================================================== */

static int current_pid = 0;

static long do_write(int fd, const void *buf, size_t count) {
    if (fd == FD_STDOUT || fd == FD_STDERR) {
        /* 翻译用户空间地址 */
        process_t *proc = &processes[current_pid];
        const char *s = as_translate(proc->as, (vaddr_t)buf, PTE_R | PTE_V);
        if (!s) {
            return -1;
        }
        for (size_t i = 0; i < count; i++) {
            console_putchar(s[i]);
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
        /* 翻译用户空间地址 */
        process_t *proc = &processes[current_pid];
        timespec_t *ktp = as_translate(proc->as, (vaddr_t)tp, PTE_W | PTE_V);
        if (!ktp) {
            return -1;
        }
        uint64_t time = read_time();
        uint64_t ns = time * 80;
        ktp->tv_sec = ns / 1000000000UL;
        ktp->tv_nsec = ns % 1000000000UL;
        return 0;
    }
    return -1;
}

static syscall_io_t io_impl;
static syscall_proc_t proc_impl;
static syscall_sched_t sched_impl;
static syscall_clock_t clock_impl;

static void init_syscall(void) {
    io_impl.write = do_write;
    io_impl.read = NULL;
    proc_impl.exit = do_exit;
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
 * 创建进程
 * ========================================================================== */

/* 将内核空间映射到用户地址空间（S-mode 才能访问） */
static void map_kernel_to_user(address_space_t *user_as, kernel_layout_t *layout, uintptr_t memory_end) {
    /* 映射内核代码段（仅 S-mode 可访问） */
    uintptr_t text_start = layout->text;
    uintptr_t text_end = layout->rodata;
    as_map_extern(user_as,
                  va_vpn(text_start), va_vpn(text_end),
                  pa_ppn(text_start), PTE_V | PTE_R | PTE_X);

    /* 映射内核只读数据 */
    as_map_extern(user_as,
                  va_vpn(layout->rodata), va_vpn(layout->data),
                  pa_ppn(layout->rodata), PTE_V | PTE_R);

    /* 映射内核数据和堆 */
    as_map_extern(user_as,
                  va_vpn(layout->data), va_vpn(memory_end),
                  pa_ppn(layout->data), PTE_V | PTE_R | PTE_W);
}

static kernel_layout_t g_layout;
static uintptr_t g_memory_end;

static int create_process(const uint8_t *elf_data, size_t elf_len) {
    if (process_count >= MAX_PROCESSES) {
        return -1;
    }

    /* 创建地址空间 */
    address_space_t *as = as_create();
    if (!as) {
        return -1;
    }

    /* 映射内核空间（陷阱处理需要） */
    map_kernel_to_user(as, &g_layout, g_memory_end);

    /* 加载 ELF */
    uintptr_t entry = elf_load(as, elf_data, elf_len);
    if (!entry) {
        as_destroy(as);
        return -1;
    }

    /* 分配并映射用户栈 */
    uintptr_t stack_vpn_end = va_vpn(USER_STACK_TOP);
    uintptr_t stack_vpn_start = stack_vpn_end - (USER_STACK_SIZE / PAGE_SIZE);
    as_map(as, stack_vpn_start, stack_vpn_end, NULL, 0, 0,
           PTE_V | PTE_R | PTE_W | PTE_U);

    /* 初始化进程 */
    int pid = process_count++;
    process_t *proc = &processes[pid];
    proc->as = as;
    proc->valid = true;

    /* 初始化上下文 */
    proc->ctx.ctx = context_user(entry);
    proc->ctx.satp = make_satp(as_root_ppn(as));
    ctx_set_sp(&proc->ctx.ctx, USER_STACK_TOP);

    printf("[INFO] created process %d, entry=%p\n", pid, (void *)entry);
    return pid;
}

/* ============================================================================
 * 创建内核地址空间
 * ========================================================================== */

static void create_kernel_space(kernel_layout_t *layout, uintptr_t memory_end) {
    kernel_as = as_create();
    map_kernel_to_user(kernel_as, layout, memory_end);
    printf("[INFO] kernel space created, root_ppn=%p\n",
           (void *)as_root_ppn(kernel_as));
}

/* ============================================================================
 * 主函数
 * ========================================================================== */

void main(void) {
    /* 获取内核布局并清零 BSS */
    kernel_layout_t layout = kernel_layout();
    clear_bss(&layout);

    /* 保存到全局变量（clear_bss 之后） */
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
    create_kernel_space(&g_layout, g_memory_end);

    /* 初始化系统调用 */
    init_syscall();

    /* 加载应用程序 */
    const app_meta_t *meta = apps_meta();
    if (!meta) {
        puts("[PANIC] No applications found");
        shutdown();
    }

    app_iter_t iter = apps_iter(meta);
    size_t app_size;
    int app_id = 0;

    while (process_count < MAX_PROCESSES) {
        const uint8_t *app = apps_next(&iter, &app_size);
        if (!app) break;

        printf("[INFO] detect app[%d]: %p..%p\n",
               app_id, (void *)app, (void *)(app + app_size));

        if (create_process(app, app_size) < 0) {
            printf("[ERROR] failed to create process for app[%d]\n", app_id);
        }

        app_id++;
    }

    puts("");

    /* 启用分页 */
    write_satp(make_satp(as_root_ppn(kernel_as)));
    puts("[INFO] paging enabled");

    /* 调度执行 */
    while (process_count > 0) {
        /* 找到一个有效进程 */
        int pid = -1;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].valid) {
                pid = i;
                break;
            }
        }
        if (pid < 0) break;

        current_pid = pid;
        process_t *proc = &processes[pid];

        /* 执行进程 */
        foreign_ctx_run(&proc->ctx);

        uintptr_t scause = read_scause();
        uintptr_t code = cause_code(scause);

        if (is_exception(scause) && code == EXCEP_U_ECALL) {
            /* 系统调用 */
            context_t *ctx = &proc->ctx.ctx;
            uintptr_t args[6];
            for (int i = 0; i < 6; i++) {
                args[i] = ctx_arg(ctx, i);
            }
            uintptr_t id = ctx_arg(ctx, 7);

            syscall_result_t ret = syscall_dispatch(id, args);

            if (id == SYS_EXIT) {
                printf("[INFO] process %d exit with code %d\n", pid, (int)args[0]);
                proc->valid = false;
                process_count--;
            } else if (ret.status == SYSCALL_OK) {
                ctx_set_arg(ctx, 0, ret.value);
                ctx_move_next(ctx);
            } else {
                printf("[ERROR] process %d unsupported syscall %d\n", pid, (int)id);
                proc->valid = false;
                process_count--;
            }
        } else if (is_exception(scause)) {
            printf("[ERROR] process %d killed: %s, stval=%p, sepc=%p\n",
                   pid, exception_name(code),
                   (void *)read_stval(), (void *)ctx_pc(&proc->ctx.ctx));
            proc->valid = false;
            process_count--;
        } else {
            printf("[ERROR] process %d killed: unexpected interrupt %d\n",
                   pid, (int)code);
            proc->valid = false;
            process_count--;
        }
    }

    shutdown();
}
