/**
 * 地址空间管理实现
 */
#include "address_space.h"
#include "../kernel-alloc/heap.h"
#include <string.h>

/* 分配一个清零的物理页 */
static void *alloc_page(void) {
    void *page = heap_alloc(PAGE_SIZE, PAGE_SIZE);
    if (page) {
        memset(page, 0, PAGE_SIZE);
    }
    return page;
}

/* 分配多个连续的清零物理页 */
static void *alloc_pages(size_t count) {
    void *pages = heap_alloc(count * PAGE_SIZE, PAGE_SIZE);
    if (pages) {
        memset(pages, 0, count * PAGE_SIZE);
    }
    return pages;
}

address_space_t *as_create(void) {
    address_space_t *as = heap_alloc(sizeof(address_space_t), 8);
    if (!as) return NULL;

    as->root = alloc_page();
    if (!as->root) {
        heap_free(as, sizeof(address_space_t));
        return NULL;
    }

    return as;
}

void as_destroy(address_space_t *as) {
    /* TODO: 递归释放所有页表页和数据页 */
    if (as) {
        heap_free(as, sizeof(address_space_t));
    }
}

uintptr_t as_root_ppn(const address_space_t *as) {
    return pa_ppn((paddr_t)as->root);
}

pte_t *as_root(const address_space_t *as) {
    return as->root;
}

/**
 * 查找或创建页表项
 * 返回最终级别的 PTE 指针
 */
static pte_t *walk(address_space_t *as, uintptr_t vpn, int create) {
    pte_t *pt = as->root;

    for (int level = 0; level < LEVELS - 1; level++) {
        uintptr_t idx = vpn_index(vpn, level);
        pte_t *pte = &pt[idx];

        if (pte_valid(*pte)) {
            /* 已有页表项，跟随到下一级 */
            pt = (pte_t *)ppn_to_pa(pte_ppn(*pte));
        } else if (create) {
            /* 分配新的页表页 */
            pte_t *new_pt = alloc_page();
            if (!new_pt) return NULL;
            *pte = make_pte(pa_ppn((paddr_t)new_pt), PTE_V);
            pt = new_pt;
        } else {
            return NULL;
        }
    }

    /* 返回最后一级的 PTE */
    return &pt[vpn_index(vpn, LEVELS - 1)];
}

void as_map_extern(address_space_t *as,
                   uintptr_t vpn_start, uintptr_t vpn_end,
                   uintptr_t ppn_base, uint64_t flags) {
    for (uintptr_t vpn = vpn_start; vpn < vpn_end; vpn++) {
        pte_t *pte = walk(as, vpn, 1);
        if (pte) {
            uintptr_t ppn = ppn_base + (vpn - vpn_start);
            *pte = make_pte(ppn, flags | PTE_V);
        }
    }
}

void as_map(address_space_t *as,
            uintptr_t vpn_start, uintptr_t vpn_end,
            const void *data, size_t len, size_t offset,
            uint64_t flags) {
    size_t count = vpn_end - vpn_start;

    /* 分配物理页 */
    uint8_t *pages = alloc_pages(count);
    if (!pages) return;

    /* 清零并拷贝数据 */
    if (data && len > 0) {
        memcpy(pages + offset, data, len);
    }

    /* 映射 */
    as_map_extern(as, vpn_start, vpn_end, pa_ppn((paddr_t)pages), flags);
}

void *as_translate(const address_space_t *as, vaddr_t va, uint64_t required_flags) {
    uintptr_t vpn = va_vpn(va);
    pte_t *pt = as->root;

    for (int level = 0; level < LEVELS; level++) {
        uintptr_t idx = vpn_index(vpn, level);
        pte_t pte = pt[idx];

        if (!pte_valid(pte)) {
            return NULL;
        }

        if (pte_is_leaf(pte)) {
            /* 检查权限 */
            if ((pte_flags(pte) & required_flags) != required_flags) {
                return NULL;
            }
            /* 计算物理地址 */
            paddr_t pa = ppn_to_pa(pte_ppn(pte)) + va_offset(va);
            return (void *)pa;
        }

        /* 继续到下一级 */
        pt = (pte_t *)ppn_to_pa(pte_ppn(pte));
    }

    return NULL;
}

/* 递归复制页表 */
static pte_t *clone_page_table(const pte_t *src, int level) {
    pte_t *dst = alloc_page();
    if (!dst) return NULL;

    for (int i = 0; i < PTE_PER_PAGE; i++) {
        pte_t pte = src[i];
        if (!pte_valid(pte)) {
            dst[i] = 0;
            continue;
        }

        if (pte_is_leaf(pte)) {
            /* 叶子节点：复制数据页 */
            uint64_t flags = pte_flags(pte);

            /* 只复制用户页（有 U 标志的页） */
            if (flags & PTE_U) {
                uint8_t *src_page = (uint8_t *)ppn_to_pa(pte_ppn(pte));
                uint8_t *dst_page = alloc_page();
                if (!dst_page) return NULL;
                memcpy(dst_page, src_page, PAGE_SIZE);
                dst[i] = make_pte(pa_ppn((paddr_t)dst_page), flags);
            } else {
                /* 内核页：共享 (保持同样的 PPN) */
                dst[i] = pte;
            }
        } else {
            /* 非叶子节点：递归复制子页表 */
            if (level < LEVELS - 1) {
                pte_t *child_src = (pte_t *)ppn_to_pa(pte_ppn(pte));
                pte_t *child_dst = clone_page_table(child_src, level + 1);
                if (!child_dst) return NULL;
                dst[i] = make_pte(pa_ppn((paddr_t)child_dst), pte_flags(pte));
            }
        }
    }

    return dst;
}

address_space_t *as_clone(const address_space_t *src) {
    address_space_t *dst = heap_alloc(sizeof(address_space_t), 8);
    if (!dst) return NULL;

    dst->root = clone_page_table(src->root, 0);
    if (!dst->root) {
        heap_free(dst, sizeof(address_space_t));
        return NULL;
    }

    return dst;
}
