/**
 * 链接器辅助实现
 */
#include "linker.h"

/* 汇编函数，避免 GOT 访问 */
extern const app_meta_t *asm_get_apps(void);
extern uintptr_t asm_get_symbol(int index);

const app_meta_t *apps_meta(void) {
    return asm_get_apps();
}

app_iter_t apps_iter(const app_meta_t *meta) {
    return (app_iter_t){.meta = meta, .index = 0};
}

const uint8_t *apps_next(app_iter_t *iter, size_t *size) {
    if (iter->index >= iter->meta->count) {
        return NULL;
    }

    uint64_t i = iter->index++;
    const uintptr_t *addrs = (const uintptr_t *)(&iter->meta->first);

    uintptr_t start = addrs[i];
    uintptr_t end = addrs[i + 1];
    *size = end - start;

    uintptr_t dest = iter->meta->base + i * iter->meta->step;
    if (dest != 0) {
        /* 复制到目标地址 */
        volatile uint8_t *d = (volatile uint8_t *)dest;
        const uint8_t *s = (const uint8_t *)start;

        for (size_t j = 0; j < *size; j++) {
            d[j] = s[j];
        }

        /* 清除指令缓存 */
        asm volatile("fence.i");

        /* 清零剩余空间 */
        for (size_t j = *size; j < 0x200000; j++) {
            d[j] = 0;
        }

        return (const uint8_t *)dest;
    }

    return (const uint8_t *)start;
}

kernel_layout_t kernel_layout(void) {
    return (kernel_layout_t){
        .text      = asm_get_symbol(0),
        .rodata    = asm_get_symbol(1),
        .data      = asm_get_symbol(2),
        .bss_start = asm_get_symbol(3),
        .bss_end   = asm_get_symbol(4),
        .boot      = asm_get_symbol(5),
        .end       = asm_get_symbol(6),
    };
}

void clear_bss(const kernel_layout_t *layout) {
    volatile uint8_t *p = (volatile uint8_t *)layout->bss_start;
    volatile uint8_t *end = (volatile uint8_t *)layout->bss_end;
    while (p < end) {
        *p++ = 0;
    }
}
