/**
 * ch3 - 多道程序与时间片轮转
 *
 * 支持多个应用同时驻留内存，通过时钟中断实现抢占式调度。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../kernel-context/context.h"
#include "../linker/linker.h"
#include "../syscall/syscall.h"
#include "../util/printf.h"
#include "../util/riscv.h"
#include "../util/sbi.h"

/* ============================================================================
 * 配置
 * ========================================================================== */

#define MAX_APPS        32
#define TIMER_INTERVAL  12500   /* 时钟中断间隔 (cycles) */

/* ============================================================================
 * 任务控制块
 * ========================================================================== */

typedef struct {
    context_t ctx;
    bool finished;
    uintptr_t stack[512];       /* 每个任务独立的用户栈 */
} task_t;

static task_t tasks[MAX_APPS];
static int task_count = 0;

/* ============================================================================
 * 系统调用实现
 * ========================================================================== */

static long do_write(int fd, const void *buf, size_t count) {
    if (fd == FD_STDOUT || fd == FD_STDERR) {
        const char *s = buf;
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
        /* QEMU virt 平台时钟频率 12.5 MHz */
        uint64_t time = read_time();
        uint64_t ns = time * 80;    /* 1 cycle = 80ns at 12.5MHz */
        tp->tv_sec = ns / 1000000000UL;
        tp->tv_nsec = ns % 1000000000UL;
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
 * 调度事件
 * ========================================================================== */

typedef enum {
    SCHED_NONE,         /* 继续执行当前任务 */
    SCHED_YIELD,        /* 主动让出 CPU */
    SCHED_EXIT,         /* 任务退出 */
    SCHED_ERROR         /* 错误 (如不支持的系统调用) */
} sched_event_t;

/* 处理系统调用，返回调度事件 */
static sched_event_t handle_syscall(task_t *task, int *exit_code) {
    uintptr_t args[6];
    for (int i = 0; i < 6; i++) {
        args[i] = ctx_arg(&task->ctx, i);
    }
    uintptr_t id = ctx_arg(&task->ctx, 7);

    syscall_result_t ret = syscall_dispatch(id, args);

    if (ret.status == SYSCALL_UNSUPPORTED) {
        return SCHED_ERROR;
    }

    switch (id) {
    case SYS_EXIT:
        *exit_code = args[0];
        return SCHED_EXIT;

    case SYS_SCHED_YIELD:
        ctx_set_arg(&task->ctx, 0, ret.value);
        ctx_move_next(&task->ctx);
        return SCHED_YIELD;

    default:
        ctx_set_arg(&task->ctx, 0, ret.value);
        ctx_move_next(&task->ctx);
        return SCHED_NONE;
    }
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
 * 设置下一次时钟中断
 * ========================================================================== */

static void set_next_timer(void) {
    sbi_set_timer(read_time() + TIMER_INTERVAL);
}

static void cancel_timer(void) {
    sbi_set_timer(UINT64_MAX);
}

/* ============================================================================
 * 主函数
 * ========================================================================== */

void main(void) {
    /* 清零 BSS */
    kernel_layout_t layout = kernel_layout();
    clear_bss(&layout);

    /* 初始化系统调用 */
    init_syscall();

    puts("");

    /* 加载应用程序 */
    const app_meta_t *meta = apps_meta();
    if (!meta) {
        puts("[PANIC] No applications found");
        shutdown();
    }

    app_iter_t iter = apps_iter(meta);
    size_t app_size;

    while (task_count < MAX_APPS) {
        const uint8_t *app = apps_next(&iter, &app_size);
        if (!app) break;

        printf("[INFO] load app%d to %p\n", task_count, (void *)app);

        task_t *task = &tasks[task_count];
        task->ctx = context_user((uintptr_t)app);
        task->finished = false;
        ctx_set_sp(&task->ctx, (uintptr_t)&task->stack[512]);

        task_count++;
    }

    puts("");

    /* 开启定时器中断 */
    enable_timer_interrupt();

    /* 轮转调度 */
    int remain = task_count;
    int current = 0;

    while (remain > 0) {
        task_t *task = &tasks[current];

        if (!task->finished) {
            /* 设置时钟中断 */
            set_next_timer();

            /* 执行任务 */
            ctx_run(&task->ctx);

            uintptr_t scause = read_scause();
            uintptr_t code = cause_code(scause);

            if (is_interrupt(scause) && code == INTR_S_TIMER) {
                /* 时钟中断：切换到下一个任务 */
                cancel_timer();
            } else if (is_exception(scause) && code == EXCEP_U_ECALL) {
                /* 系统调用 */
                int exit_code = 0;
                sched_event_t event = handle_syscall(task, &exit_code);

                switch (event) {
                case SCHED_NONE:
                    continue;   /* 继续执行当前任务 */

                case SCHED_YIELD:
                    /* 主动让出，切换任务 */
                    break;

                case SCHED_EXIT:
                    printf("[INFO] app%d exit with code %d\n", current, exit_code);
                    task->finished = true;
                    remain--;
                    break;

                case SCHED_ERROR:
                    printf("[ERROR] app%d unsupported syscall\n", current);
                    task->finished = true;
                    remain--;
                    break;
                }
            } else if (is_exception(scause)) {
                /* 其他异常 */
                printf("[ERROR] app%d killed: Exception(%s)\n",
                       current, exception_name(code));
                task->finished = true;
                remain--;
            } else {
                /* 其他中断 */
                printf("[ERROR] app%d killed: unexpected interrupt %d\n",
                       current, (int)code);
                task->finished = true;
                remain--;
            }
        }

        /* 轮转到下一个任务 */
        current = (current + 1) % task_count;
    }

    shutdown();
}
