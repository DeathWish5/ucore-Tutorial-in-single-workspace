/**
 * 链接器辅助：内核布局和应用程序加载
 */
#ifndef LINKER_H
#define LINKER_H

#include <stddef.h>
#include <stdint.h>

/**
 * 应用程序元数据
 *
 * 由 gen_app_asm.sh 生成，嵌入在内核数据段中。
 * first 之后紧跟 count+1 个地址，分别是各应用的起始和最后一个的结束。
 */
typedef struct {
    uint64_t base;      /* 加载目标基地址 */
    uint64_t step;      /* 每个应用间隔 (0 表示不复制) */
    uint64_t count;     /* 应用数量 */
    uint64_t first;     /* 第一个位置（作为数组起始） */
} app_meta_t;

/* 应用迭代器 */
typedef struct {
    const app_meta_t *meta;
    uint64_t index;
} app_iter_t;

/* 获取应用元数据 */
const app_meta_t *apps_meta(void);

/* 创建迭代器 */
app_iter_t apps_iter(const app_meta_t *meta);

/* 获取下一个应用，返回加载后的地址和大小，NULL 表示结束 */
const uint8_t *apps_next(app_iter_t *iter, size_t *size);

/**
 * 内核内存布局
 */
typedef struct {
    uintptr_t text;
    uintptr_t rodata;
    uintptr_t data;
    uintptr_t bss_start;
    uintptr_t bss_end;
    uintptr_t boot;
    uintptr_t end;
} kernel_layout_t;

/* 获取内核布局 */
kernel_layout_t kernel_layout(void);

/* 清零 BSS 段 */
void clear_bss(const kernel_layout_t *layout);

#endif /* LINKER_H */
