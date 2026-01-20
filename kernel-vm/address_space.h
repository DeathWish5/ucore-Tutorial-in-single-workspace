/**
 * 地址空间管理
 *
 * 对应 Rust 的 AddressSpace<Sv39, Sv39Manager>
 */
#ifndef ADDRESS_SPACE_H
#define ADDRESS_SPACE_H

#include "sv39.h"
#include <stddef.h>

/* ============================================================================
 * 地址空间结构
 * ========================================================================== */

typedef struct {
    pte_t *root;                /* 根页表指针 (物理地址 = 虚拟地址) */
} address_space_t;

/* ============================================================================
 * API
 * ========================================================================== */

/**
 * 创建新地址空间
 */
address_space_t *as_create(void);

/**
 * 销毁地址空间
 */
void as_destroy(address_space_t *as);

/**
 * 获取根页表 PPN
 */
uintptr_t as_root_ppn(const address_space_t *as);

/**
 * 获取根页表指针 (用于共享页表项)
 */
pte_t *as_root(const address_space_t *as);

/**
 * 映射已存在的物理页 (不分配)
 *
 * @param as        地址空间
 * @param vpn_start 起始虚拟页号
 * @param vpn_end   结束虚拟页号 (不包含)
 * @param ppn_base  物理页号基址
 * @param flags     页表项标志
 */
void as_map_extern(address_space_t *as,
                   uintptr_t vpn_start, uintptr_t vpn_end,
                   uintptr_t ppn_base, uint64_t flags);

/**
 * 分配物理页并映射，拷贝数据
 *
 * @param as        地址空间
 * @param vpn_start 起始虚拟页号
 * @param vpn_end   结束虚拟页号 (不包含)
 * @param data      要拷贝的数据
 * @param len       数据长度
 * @param offset    数据在首页中的偏移
 * @param flags     页表项标志
 */
void as_map(address_space_t *as,
            uintptr_t vpn_start, uintptr_t vpn_end,
            const void *data, size_t len, size_t offset,
            uint64_t flags);

/**
 * 地址翻译：检查权限并返回物理地址
 *
 * @param as             地址空间
 * @param va             虚拟地址
 * @param required_flags 要求的权限标志
 * @return 物理地址指针，失败返回 NULL
 */
void *as_translate(const address_space_t *as, vaddr_t va, uint64_t required_flags);

/**
 * 复制地址空间（用于 fork）
 *
 * @param src 源地址空间
 * @return 新的地址空间，失败返回 NULL
 */
address_space_t *as_clone(const address_space_t *src);

#endif /* ADDRESS_SPACE_H */
