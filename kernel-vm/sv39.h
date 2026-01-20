/**
 * Sv39 页表定义
 *
 * RISC-V Sv39 分页方案：39 位虚拟地址，3 级页表
 */
#ifndef SV39_H
#define SV39_H

#include <stdint.h>

/* ============================================================================
 * Sv39 参数 (对应 Rust VmMeta trait)
 * ========================================================================== */

#define PAGE_BITS       12              /* 页大小: 4KB */
#define PAGE_SIZE       (1UL << PAGE_BITS)
#define PAGE_MASK       (PAGE_SIZE - 1)

#define LEVELS          3               /* 3 级页表 */
#define PTE_PER_PAGE    512             /* 每页 512 个 PTE */
#define VPN_BITS        9               /* 每级 VPN 9 位 */

#define VA_BITS         39              /* 虚拟地址宽度 */
#define PA_BITS         56              /* 物理地址宽度 */
#define PPN_BITS        44              /* 物理页号宽度 */

/* ============================================================================
 * 地址类型
 * ========================================================================== */

typedef uintptr_t vaddr_t;              /* 虚拟地址 */
typedef uintptr_t paddr_t;              /* 物理地址 */
typedef uint64_t  pte_t;                /* 页表项 */

/* ============================================================================
 * 地址转换
 * ========================================================================== */

/* 虚拟地址 <-> VPN */
static inline uintptr_t va_vpn(vaddr_t va) {
    return (va >> PAGE_BITS) & ((1UL << 27) - 1);
}

static inline vaddr_t vpn_to_va(uintptr_t vpn) {
    return vpn << PAGE_BITS;
}

/* 物理地址 <-> PPN */
static inline uintptr_t pa_ppn(paddr_t pa) {
    return pa >> PAGE_BITS;
}

static inline paddr_t ppn_to_pa(uintptr_t ppn) {
    return ppn << PAGE_BITS;
}

/* 提取 VPN 各级索引 (level: 0=最高级, 2=最低级) */
static inline uintptr_t vpn_index(uintptr_t vpn, int level) {
    return (vpn >> (VPN_BITS * (2 - level))) & (PTE_PER_PAGE - 1);
}

/* 虚拟地址页内偏移 */
static inline uintptr_t va_offset(vaddr_t va) {
    return va & PAGE_MASK;
}

/* ============================================================================
 * 页表项 (PTE) 标志位
 * ========================================================================== */

#define PTE_V       (1UL << 0)          /* Valid */
#define PTE_R       (1UL << 1)          /* Readable */
#define PTE_W       (1UL << 2)          /* Writable */
#define PTE_X       (1UL << 3)          /* Executable */
#define PTE_U       (1UL << 4)          /* User accessible */
#define PTE_G       (1UL << 5)          /* Global */
#define PTE_A       (1UL << 6)          /* Accessed */
#define PTE_D       (1UL << 7)          /* Dirty */
#define PTE_RSW     (3UL << 8)          /* Reserved for software */

/* 从 PTE 提取 PPN */
static inline uintptr_t pte_ppn(pte_t pte) {
    return (pte >> 10) & ((1UL << PPN_BITS) - 1);
}

/* 从 PTE 提取标志 */
static inline uint64_t pte_flags(pte_t pte) {
    return pte & 0x3FF;
}

/* 构建 PTE */
static inline pte_t make_pte(uintptr_t ppn, uint64_t flags) {
    return (ppn << 10) | flags;
}

/* PTE 判断 */
static inline int pte_valid(pte_t pte) {
    return (pte & PTE_V) != 0;
}

static inline int pte_is_leaf(pte_t pte) {
    return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}

/* ============================================================================
 * SATP 寄存器
 * ========================================================================== */

#define SATP_MODE_SV39  (8UL << 60)

static inline uint64_t make_satp(uintptr_t root_ppn) {
    return SATP_MODE_SV39 | root_ppn;
}

static inline void write_satp(uint64_t satp) {
    asm volatile("csrw satp, %0" :: "r"(satp));
    asm volatile("sfence.vma");
}

static inline uint64_t read_satp(void) {
    uint64_t val;
    asm volatile("csrr %0, satp" : "=r"(val));
    return val;
}

#endif /* SV39_H */
