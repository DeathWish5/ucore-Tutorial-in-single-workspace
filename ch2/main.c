/**
 * ch2 - 批处理系统
 *
 * 依次加载并执行用户程序，处理系统调用和异常。
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
    /* 由主循环处理 */
}

static syscall_io_t io_impl;
static syscall_proc_t proc_impl;

static void init_syscall(void) {
    io_impl.write = do_write;
    io_impl.read = NULL;
    proc_impl.exit = do_exit;
    syscall_set_io(&io_impl);
    syscall_set_proc(&proc_impl);
}

/* ============================================================================
 * 异常处理
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

int main(void) {
    /* 清零 BSS */
    kernel_layout_t layout = kernel_layout();
    clear_bss(&layout);

    /* 初始化系统调用 */
    init_syscall();

    puts("");

    /* 加载应用程序元数据 */
    const app_meta_t *meta = apps_meta();
    if (!meta) {
        puts("[PANIC] No applications found");
        shutdown();
    }

    /* 批处理执行 */
    app_iter_t iter = apps_iter(meta);
    size_t app_size;
    int app_id = 0;

    while (true) {
        const uint8_t *app = apps_next(&iter, &app_size);
        if (!app) break;

        printf("[INFO] load app%d to %p\n", app_id, (void *)app);

        /* 初始化上下文 */
        context_t ctx = context_user((uintptr_t)app);

        /* 分配用户栈 */
        static uintptr_t stacks[16][256];
        ctx_set_sp(&ctx, (uintptr_t)&stacks[app_id % 16][256]);

        asm volatile("fence.i");

        /* 运行用户程序 */
        while (true) {
            ctx_run(&ctx);

            uintptr_t scause = read_scause();
            uintptr_t code = cause_code(scause);

            if (is_exception(scause) && code == EXCEP_U_ECALL) {
                /* 系统调用 */
                uintptr_t args[6];
                for (int i = 0; i < 6; i++) {
                    args[i] = ctx_arg(&ctx, i);
                }

                uintptr_t id = ctx_arg(&ctx, 7);
                syscall_result_t ret = syscall_dispatch(id, args);

                if (id == SYS_EXIT) {
                    printf("[INFO] app%d exit with code %d\n", app_id, (int)args[0]);
                    break;
                }

                if (ret.status == SYSCALL_OK) {
                    ctx_set_arg(&ctx, 0, ret.value);
                    ctx_move_next(&ctx);
                } else {
                    printf("[ERROR] app%d unsupported syscall %d\n", app_id, (int)id);
                    break;
                }
            } else {
                /* 其他异常 */
                printf("[ERROR] app%d killed: Exception(%s)\n", app_id, exception_name(code));
                break;
            }
        }

        puts("");
        app_id++;
    }

    shutdown();
    __builtin_unreachable();
}
