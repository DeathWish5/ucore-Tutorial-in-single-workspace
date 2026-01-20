/**
 * 线程上下文管理
 *
 * 支持同地址空间和跨地址空间执行。
 */
#ifndef CONTEXT_H
#define CONTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * 线程上下文结构
 *
 * 内存布局（用于汇编访问）：
 *   偏移 0:   sctx (调度上下文指针)
 *   偏移 8:   x[31] (通用寄存器, 248 bytes)
 *   偏移 256: sepc
 *   偏移 264: supervisor
 *   偏移 265: interrupt
 */
typedef struct {
    uintptr_t sctx;         /* 调度上下文指针 */
    uintptr_t x[31];        /* 通用寄存器 x1-x31 */
    uintptr_t sepc;         /* 异常程序计数器 */
    bool supervisor;        /* true: S-mode, false: U-mode */
    bool interrupt;         /* sret 后是否开启中断 */
} context_t;

/**
 * 跨地址空间上下文
 *
 * 用于在不同地址空间中执行线程
 * 注意：需要额外空间保存内核 satp 和 stvec
 */
typedef struct {
    context_t ctx;          /* 线程上下文 (266 bytes, 对齐到 272) */
    uint64_t satp;          /* 目标地址空间的 satp (+272) */
    uint64_t kernel_satp;   /* 内核 satp 暂存 (+280) */
    uint64_t kernel_stvec;  /* 内核 stvec 暂存 (+288) */
} foreign_ctx_t;

/* ============================================================================
 * 基本上下文操作
 * ========================================================================== */

/* 创建用户态上下文 */
context_t context_user(uintptr_t entry);

/* 创建内核线程上下文 */
context_t context_thread(uintptr_t entry, bool interrupt);

/* 通用寄存器访问 (n: 1-31) */
uintptr_t ctx_reg(const context_t *ctx, size_t n);
void ctx_set_reg(context_t *ctx, size_t n, uintptr_t val);

/* 参数寄存器访问 (n: 0-7 对应 a0-a7) */
uintptr_t ctx_arg(const context_t *ctx, size_t n);
void ctx_set_arg(context_t *ctx, size_t n, uintptr_t val);

/* 栈指针访问 */
uintptr_t ctx_sp(const context_t *ctx);
void ctx_set_sp(context_t *ctx, uintptr_t sp);

/* 程序计数器访问 */
uintptr_t ctx_pc(const context_t *ctx);
void ctx_set_pc(context_t *ctx, uintptr_t pc);

/* PC 前进到下一条指令 */
void ctx_move_next(context_t *ctx);

/* ============================================================================
 * 执行
 * ========================================================================== */

/**
 * 执行上下文（同地址空间）
 */
void ctx_run(context_t *ctx);

/**
 * 执行跨地址空间上下文
 *
 * 会切换 satp 到目标地址空间执行，陷阱时切换回来
 */
void foreign_ctx_run(foreign_ctx_t *ctx);

#endif /* CONTEXT_H */
