/**
 * RISC-V CSR 寄存器操作
 */
#ifndef RISCV_H
#define RISCV_H

#include <stdint.h>

/* scause 异常码 */
#define EXCEP_INSTRUCTION_MISALIGNED    0
#define EXCEP_INSTRUCTION_FAULT         1
#define EXCEP_ILLEGAL_INSTRUCTION       2
#define EXCEP_BREAKPOINT                3
#define EXCEP_LOAD_MISALIGNED           4
#define EXCEP_LOAD_FAULT                5
#define EXCEP_STORE_MISALIGNED          6
#define EXCEP_STORE_FAULT               7
#define EXCEP_U_ECALL                   8
#define EXCEP_S_ECALL                   9
#define EXCEP_INSTRUCTION_PAGE_FAULT    12
#define EXCEP_LOAD_PAGE_FAULT           13
#define EXCEP_STORE_PAGE_FAULT          15

/* scause 中断码 */
#define INTR_S_SOFT     1
#define INTR_S_TIMER    5
#define INTR_S_EXT      9

#define SCAUSE_INTERRUPT    (1UL << 63)

/* 读取 CSR */
static inline uintptr_t read_scause(void) {
    uintptr_t val;
    asm volatile("csrr %0, scause" : "=r"(val));
    return val;
}

static inline uintptr_t read_stval(void) {
    uintptr_t val;
    asm volatile("csrr %0, stval" : "=r"(val));
    return val;
}

static inline uintptr_t read_sepc(void) {
    uintptr_t val;
    asm volatile("csrr %0, sepc" : "=r"(val));
    return val;
}

static inline uint64_t read_time(void) {
    uint64_t val;
    asm volatile("rdtime %0" : "=r"(val));
    return val;
}

static inline uintptr_t read_sie(void) {
    uintptr_t val;
    asm volatile("csrr %0, sie" : "=r"(val));
    return val;
}

/* 写入 CSR */
static inline void write_sie(uintptr_t val) {
    asm volatile("csrw sie, %0" :: "r"(val));
}

/* 中断使能位 */
#define SIE_STIE    (1 << 5)    /* S-mode 定时器中断 */
#define SIE_SEIE    (1 << 9)    /* S-mode 外部中断 */
#define SIE_SSIE    (1 << 1)    /* S-mode 软件中断 */

/* 开启 S-mode 定时器中断 */
static inline void enable_timer_interrupt(void) {
    write_sie(read_sie() | SIE_STIE);
}

/* 关闭 S-mode 定时器中断 */
static inline void disable_timer_interrupt(void) {
    write_sie(read_sie() & ~SIE_STIE);
}

/* 判断是否是中断 */
static inline int is_interrupt(uintptr_t scause) {
    return (scause & SCAUSE_INTERRUPT) != 0;
}

/* 判断是否是异常 */
static inline int is_exception(uintptr_t scause) {
    return (scause & SCAUSE_INTERRUPT) == 0;
}

/* 获取异常/中断码 */
static inline uintptr_t cause_code(uintptr_t scause) {
    return scause & ~SCAUSE_INTERRUPT;
}

#endif /* RISCV_H */
