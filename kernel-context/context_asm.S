/**
 * 上下文切换汇编实现
 *
 * context_t 内存布局:
 *   +0:   sctx (8B)
 *   +8:   x[31] (248B), x[n-1] 存储 xn, 偏移 = n*8
 *   +256: sepc (8B)
 *   +264: supervisor (1B)
 *   +265: interrupt (1B)
 *
 * foreign_ctx_t 内存布局 (280 字节对齐到 8):
 *   +0:   context_t
 *   +272: satp (8B)
 */

.section .text

/* 宏：保存/加载单个寄存器到 context_t */
.macro CTX_SAVE reg
    sd x\reg, \reg*8(sp)
.endm

.macro CTX_LOAD reg
    ld x\reg, \reg*8(sp)
.endm

/* 宏：保存/加载所有通用寄存器（除 x0, x2） */
.macro SAVE_GP_REGS
    CTX_SAVE 1
    CTX_SAVE 3
    CTX_SAVE 4
    CTX_SAVE 5
    CTX_SAVE 6
    CTX_SAVE 7
    CTX_SAVE 8
    CTX_SAVE 9
    CTX_SAVE 10
    CTX_SAVE 11
    CTX_SAVE 12
    CTX_SAVE 13
    CTX_SAVE 14
    CTX_SAVE 15
    CTX_SAVE 16
    CTX_SAVE 17
    CTX_SAVE 18
    CTX_SAVE 19
    CTX_SAVE 20
    CTX_SAVE 21
    CTX_SAVE 22
    CTX_SAVE 23
    CTX_SAVE 24
    CTX_SAVE 25
    CTX_SAVE 26
    CTX_SAVE 27
    CTX_SAVE 28
    CTX_SAVE 29
    CTX_SAVE 30
    CTX_SAVE 31
.endm

.macro LOAD_GP_REGS
    CTX_LOAD 1
    CTX_LOAD 3
    CTX_LOAD 4
    CTX_LOAD 5
    CTX_LOAD 6
    CTX_LOAD 7
    CTX_LOAD 8
    CTX_LOAD 9
    CTX_LOAD 10
    CTX_LOAD 11
    CTX_LOAD 12
    CTX_LOAD 13
    CTX_LOAD 14
    CTX_LOAD 15
    CTX_LOAD 16
    CTX_LOAD 17
    CTX_LOAD 18
    CTX_LOAD 19
    CTX_LOAD 20
    CTX_LOAD 21
    CTX_LOAD 22
    CTX_LOAD 23
    CTX_LOAD 24
    CTX_LOAD 25
    CTX_LOAD 26
    CTX_LOAD 27
    CTX_LOAD 28
    CTX_LOAD 29
    CTX_LOAD 30
    CTX_LOAD 31
.endm

/**
 * context_run_asm - 同地址空间上下文切换
 */
.global context_run_asm
context_run_asm:
    addi sp, sp, -256
    SAVE_GP_REGS

    mv s0, a0

    /* 构建 sstatus */
    lbu t0, 264(s0)
    lbu t1, 265(s0)
    csrr t2, sstatus
    li t3, (1 << 8)
    beqz t0, 1f
    or t2, t2, t3
    j 2f
1:  not t3, t3
    and t2, t2, t3
2:  li t3, (1 << 5)
    beqz t1, 3f
    or t2, t2, t3
    j 4f
3:  not t3, t3
    and t2, t2, t3
4:
    csrw sscratch, s0
    ld t0, 256(s0)
    csrw sepc, t0
    csrw sstatus, t2
    lla t0, .Ltrap_entry
    csrw stvec, t0

    sd sp, 0(s0)
    mv sp, s0

    LOAD_GP_REGS
    ld sp, 2*8(sp)

    sret

.align 2
.Ltrap_entry:
    csrrw sp, sscratch, sp
    SAVE_GP_REGS
    csrrw t0, sscratch, sp
    sd t0, 2*8(sp)
    csrr t0, sepc
    sd t0, 256(sp)

    ld sp, 0(sp)
    LOAD_GP_REGS
    addi sp, sp, 256

    ret

/**
 * foreign_context_run_asm - 跨地址空间上下文切换
 *
 * 策略：
 * 1. 保存内核状态到栈上
 * 2. 将内核 satp 和 stvec 保存到 ctx 结构中（会在用户空间可见）
 * 3. 切换到用户 satp
 * 4. sret 进入用户态
 * 5. 陷阱时：先保存用户寄存器，再切换回内核 satp，最后返回
 *
 * 使用 ctx 结构的 sctx 字段保存内核栈指针
 * 扩展 foreign_ctx_t：在 +280 保存内核 satp，+288 保存内核 stvec
 */
.global foreign_context_run_asm
foreign_context_run_asm:
    /* 保存 callee-saved 寄存器到内核栈 */
    addi sp, sp, -256
    SAVE_GP_REGS

    mv s0, a0           /* s0 = foreign_ctx_t * */

    /* 保存当前 satp 到 ctx+280 */
    csrr t0, satp
    sd t0, 280(s0)

    /* 保存当前 stvec 到 ctx+288 */
    csrr t0, stvec
    sd t0, 288(s0)

    /* 保存内核栈到 ctx->sctx */
    sd sp, 0(s0)

    /* 设置 stvec 为我们的陷阱入口 */
    lla t0, .Lforeign_trap
    csrw stvec, t0

    /* 设置 sscratch 为 ctx */
    csrw sscratch, s0

    /* 构建并设置 sstatus */
    lbu t0, 264(s0)
    lbu t1, 265(s0)
    csrr t2, sstatus
    li t3, (1 << 8)
    beqz t0, 1f
    or t2, t2, t3
    j 2f
1:  not t3, t3
    and t2, t2, t3
2:  li t3, (1 << 5)
    beqz t1, 3f
    or t2, t2, t3
    j 4f
3:  not t3, t3
    and t2, t2, t3
4:
    csrw sstatus, t2

    /* 设置 sepc */
    ld t0, 256(s0)
    csrw sepc, t0

    /* 切换到用户地址空间 */
    ld t0, 272(s0)
    csrw satp, t0
    sfence.vma

    /* 切换 sp 到 ctx，加载用户寄存器 */
    mv sp, s0
    LOAD_GP_REGS
    ld sp, 2*8(sp)

    sret

.align 2
.Lforeign_trap:
    /* 交换 sp 和 sscratch，sp = ctx */
    csrrw sp, sscratch, sp

    /* 保存用户寄存器到 ctx */
    SAVE_GP_REGS

    /* 保存用户 sp */
    csrrw t0, sscratch, sp
    sd t0, 2*8(sp)

    /* 保存 sepc */
    csrr t0, sepc
    sd t0, 256(sp)

    /* 切换回内核地址空间 */
    ld t0, 280(sp)
    csrw satp, t0
    sfence.vma

    /* 恢复 stvec */
    ld t0, 288(sp)
    csrw stvec, t0

    /* 恢复内核栈 */
    ld sp, 0(sp)

    /* 恢复 callee-saved 寄存器 */
    LOAD_GP_REGS
    addi sp, sp, 256

    ret
