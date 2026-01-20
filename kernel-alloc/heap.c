/**
 * 简单堆分配器实现
 *
 * Bump allocator: 只向上增长，不真正释放
 */
#include "heap.h"
#include <string.h>

static uintptr_t heap_start;
static uintptr_t heap_end;
static uintptr_t heap_current;

void heap_init(uintptr_t start, size_t size) {
    heap_start = start;
    heap_end = start + size;
    heap_current = start;
}

void *heap_alloc(size_t size, size_t align) {
    /* 对齐当前指针 */
    uintptr_t aligned = (heap_current + align - 1) & ~(align - 1);

    /* 检查是否有足够空间 */
    if (aligned + size > heap_end) {
        return NULL;
    }

    heap_current = aligned + size;
    return (void *)aligned;
}

void heap_free(void *ptr, size_t size) {
    /* Bump allocator 不真正释放 */
    (void)ptr;
    (void)size;
}

void *heap_alloc_zeroed(size_t size, size_t align) {
    void *ptr = heap_alloc(size, align);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}
