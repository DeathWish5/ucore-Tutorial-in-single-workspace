/**
 * 简单堆分配器
 *
 * 使用 bump allocator（递增分配），不支持释放。
 * 足够用于教学演示。
 */
#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

/**
 * 初始化堆
 *
 * @param start 堆起始地址
 * @param size  堆大小
 */
void heap_init(uintptr_t start, size_t size);

/**
 * 分配内存
 *
 * @param size  请求大小
 * @param align 对齐要求
 * @return 分配的内存指针，失败返回 NULL
 */
void *heap_alloc(size_t size, size_t align);

/**
 * 释放内存 (bump allocator 不真正释放)
 */
void heap_free(void *ptr, size_t size);

/**
 * 分配清零的内存
 */
void *heap_alloc_zeroed(size_t size, size_t align);

#endif /* HEAP_H */
