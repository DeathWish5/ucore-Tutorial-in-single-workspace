/**
 * 线程上下文管理实现
 */
#include "context.h"

/* 汇编实现 */
extern void context_run_asm(context_t *ctx);
extern void foreign_context_run_asm(foreign_ctx_t *ctx);

context_t context_user(uintptr_t entry) {
    context_t ctx = {0};
    ctx.sepc = entry;
    ctx.supervisor = false;
    ctx.interrupt = true;
    return ctx;
}

context_t context_thread(uintptr_t entry, bool interrupt) {
    context_t ctx = {0};
    ctx.sepc = entry;
    ctx.supervisor = true;
    ctx.interrupt = interrupt;
    return ctx;
}

uintptr_t ctx_reg(const context_t *ctx, size_t n) {
    if (n == 0 || n > 31) return 0;
    return ctx->x[n - 1];
}

void ctx_set_reg(context_t *ctx, size_t n, uintptr_t val) {
    if (n > 0 && n <= 31) {
        ctx->x[n - 1] = val;
    }
}

uintptr_t ctx_arg(const context_t *ctx, size_t n) {
    return (n <= 7) ? ctx_reg(ctx, n + 10) : 0;
}

void ctx_set_arg(context_t *ctx, size_t n, uintptr_t val) {
    if (n <= 7) {
        ctx_set_reg(ctx, n + 10, val);
    }
}

uintptr_t ctx_sp(const context_t *ctx) {
    return ctx_reg(ctx, 2);
}

void ctx_set_sp(context_t *ctx, uintptr_t sp) {
    ctx_set_reg(ctx, 2, sp);
}

uintptr_t ctx_pc(const context_t *ctx) {
    return ctx->sepc;
}

void ctx_set_pc(context_t *ctx, uintptr_t pc) {
    ctx->sepc = pc;
}

void ctx_move_next(context_t *ctx) {
    ctx->sepc += 4;
}

void ctx_run(context_t *ctx) {
    context_run_asm(ctx);
}

void foreign_ctx_run(foreign_ctx_t *ctx) {
    foreign_context_run_asm(ctx);
}
