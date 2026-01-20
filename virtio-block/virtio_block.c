/**
 * VirtIO Block 设备驱动实现
 */
#include "virtio_block.h"
#include "../kernel-alloc/heap.h"
#include <string.h>

/* MMIO 读写 */
static inline uint32_t mmio_read32(volatile uint32_t *addr) {
    return *addr;
}

static inline void mmio_write32(volatile uint32_t *addr, uint32_t val) {
    *addr = val;
}

/* 分配描述符 */
static int alloc_desc(virtio_blk_t *blk) {
    for (int i = 0; i < VIRTQ_SIZE; i++) {
        if (blk->free[i]) {
            blk->free[i] = false;
            return i;
        }
    }
    return -1;
}

static void free_desc(virtio_blk_t *blk, int i) {
    blk->free[i] = true;
}

static void free_chain(virtio_blk_t *blk, int i) {
    while (1) {
        int flags = blk->desc[i].flags;
        int next = blk->desc[i].next;
        free_desc(blk, i);
        if (flags & VIRTQ_DESC_F_NEXT) {
            i = next;
        } else {
            break;
        }
    }
}

/* 简易调试输出 */
extern void console_putchar(int ch);
static void debug_puts(const char *s) {
    while (*s) console_putchar(*s++);
    console_putchar('\n');
}
static void debug_hex(uint32_t v) {
    const char *hex = "0123456789abcdef";
    console_putchar('0'); console_putchar('x');
    for (int i = 28; i >= 0; i -= 4) {
        console_putchar(hex[(v >> i) & 0xf]);
    }
    console_putchar('\n');
}

int virtio_blk_init(virtio_blk_t *blk) {
    blk->regs = (volatile uint32_t *)VIRTIO0_BASE;

    /* 检查 magic */
    uint32_t magic = mmio_read32(blk->regs + VIRTIO_MMIO_MAGIC_VALUE/4);
    debug_puts("[VIRTIO] magic:");
    debug_hex(magic);
    if (magic != 0x74726976) {
        debug_puts("[VIRTIO] bad magic");
        return -1;
    }

    /* 检查版本和设备类型 */
    uint32_t version = mmio_read32(blk->regs + VIRTIO_MMIO_VERSION/4);
    debug_puts("[VIRTIO] version:");
    debug_hex(version);
    if (version != 1 && version != 2) {
        debug_puts("[VIRTIO] unsupported version");
        return -1;
    }
    uint32_t device_id = mmio_read32(blk->regs + VIRTIO_MMIO_DEVICE_ID/4);
    debug_puts("[VIRTIO] device_id:");
    debug_hex(device_id);
    if (device_id != 2) {
        debug_puts("[VIRTIO] not a block device");
        return -1; /* 不是块设备 */
    }

    /* 重置设备 */
    mmio_write32(blk->regs + VIRTIO_MMIO_STATUS/4, 0);

    /* 设置 ACKNOWLEDGE 状态 */
    mmio_write32(blk->regs + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACKNOWLEDGE);

    /* 设置 DRIVER 状态 */
    mmio_write32(blk->regs + VIRTIO_MMIO_STATUS/4,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* 读取并设置特性 */
    mmio_write32(blk->regs + VIRTIO_MMIO_DEVICE_FEATURES_SEL/4, 0);
    uint32_t features = mmio_read32(blk->regs + VIRTIO_MMIO_DEVICE_FEATURES/4);
    (void)features;

    mmio_write32(blk->regs + VIRTIO_MMIO_DRIVER_FEATURES_SEL/4, 0);
    mmio_write32(blk->regs + VIRTIO_MMIO_DRIVER_FEATURES/4, 0);

    /* 设置 FEATURES_OK */
    mmio_write32(blk->regs + VIRTIO_MMIO_STATUS/4,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    /* 检查 FEATURES_OK */
    uint32_t status = mmio_read32(blk->regs + VIRTIO_MMIO_STATUS/4);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        return -1;
    }

    /* 选择队列 0 */
    mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_SEL/4, 0);

    /* 检查队列是否已在使用 */
    if (mmio_read32(blk->regs + VIRTIO_MMIO_QUEUE_READY/4) != 0) {
        return -1;
    }

    /* 检查最大队列大小 */
    uint32_t max = mmio_read32(blk->regs + VIRTIO_MMIO_QUEUE_NUM_MAX/4);
    if (max < VIRTQ_SIZE) {
        return -1;
    }

    /* 分配队列内存 (页对齐)
     * Legacy 布局: desc (16*VIRTQ_SIZE) | avail (6+2*VIRTQ_SIZE) | padding | used (对齐到 PAGE)
     * desc_size = 16 * 8 = 128
     * avail_size = 6 + 2 * 8 = 22
     * used_size = 6 + 8 * 8 = 70
     * avail_end 需要对齐到页边界后放 used
     */
    size_t desc_size = VIRTQ_SIZE * sizeof(virtq_desc_t);  /* 128 */

    /* 简化：分配 2 页，desc+avail 在第一页，used 在第二页 */
    size_t total = 4096 * 2;
    uint8_t *queue_mem = heap_alloc(total, 4096);
    if (!queue_mem) return -1;
    memset(queue_mem, 0, total);

    blk->desc = (virtq_desc_t *)queue_mem;
    blk->avail = (virtq_avail_t *)(queue_mem + desc_size);
    /* 在 legacy 模式下，used 区域需要对齐到页边界 (从 queue_mem 开始的偏移) */
    /* avail 结束位置后对齐到 4096 边界 */
    blk->used = (virtq_used_t *)(queue_mem + 4096);  /* 第二页开始 */

    /* 设置队列大小 */
    mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_NUM/4, VIRTQ_SIZE);

    if (version == 1) {
        /* Legacy MMIO: 使用 QUEUE_PFN 和 GUEST_PAGE_SIZE */
        mmio_write32(blk->regs + VIRTIO_MMIO_GUEST_PAGE_SIZE/4, 4096);

        /* 在 legacy 模式下，desc, avail, used 必须在同一页内连续布局 */
        /* 布局: desc | avail | padding | used (对齐到 4096) */
        uintptr_t queue_pfn = (uintptr_t)queue_mem >> 12;
        mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_PFN/4, queue_pfn);
    } else {
        /* Modern MMIO (version 2): 使用分离的地址寄存器 */
        uint64_t desc_addr = (uint64_t)(uintptr_t)blk->desc;
        uint64_t avail_addr = (uint64_t)(uintptr_t)blk->avail;
        uint64_t used_addr = (uint64_t)(uintptr_t)blk->used;

        mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_DESC_LOW/4, desc_addr);
        mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_DESC_HIGH/4, desc_addr >> 32);
        mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_AVAIL_LOW/4, avail_addr);
        mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_AVAIL_HIGH/4, avail_addr >> 32);
        mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_USED_LOW/4, used_addr);
        mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_USED_HIGH/4, used_addr >> 32);

        /* 启用队列 */
        mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_READY/4, 1);
    }

    /* 设置 DRIVER_OK */
    mmio_write32(blk->regs + VIRTIO_MMIO_STATUS/4,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                 VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    /* 初始化描述符空闲列表 */
    for (int i = 0; i < VIRTQ_SIZE; i++) {
        blk->free[i] = true;
    }
    blk->last_used_idx = 0;

    return 0;
}

/* 执行块操作 */
static int virtio_blk_rw(virtio_blk_t *blk, size_t sector, uint8_t *buf, int write) {
    /* 准备请求头 */
    blk->req.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    blk->req.reserved = 0;
    blk->req.sector = sector;
    blk->status = 0xff;

    /* 分配 3 个描述符 */
    int idx[3];
    for (int i = 0; i < 3; i++) {
        idx[i] = alloc_desc(blk);
        if (idx[i] < 0) return -1;
    }

    /* 描述符 0: 请求头 (设备读) */
    blk->desc[idx[0]].addr = (uint64_t)(uintptr_t)&blk->req;
    blk->desc[idx[0]].len = sizeof(virtio_blk_req_t);
    blk->desc[idx[0]].flags = VIRTQ_DESC_F_NEXT;
    blk->desc[idx[0]].next = idx[1];

    /* 描述符 1: 数据缓冲区 */
    blk->desc[idx[1]].addr = (uint64_t)(uintptr_t)buf;
    blk->desc[idx[1]].len = 512;
    blk->desc[idx[1]].flags = VIRTQ_DESC_F_NEXT | (write ? 0 : VIRTQ_DESC_F_WRITE);
    blk->desc[idx[1]].next = idx[2];

    /* 描述符 2: 状态 (设备写) */
    blk->desc[idx[2]].addr = (uint64_t)(uintptr_t)&blk->status;
    blk->desc[idx[2]].len = 1;
    blk->desc[idx[2]].flags = VIRTQ_DESC_F_WRITE;
    blk->desc[idx[2]].next = 0;

    /* 添加到可用环 */
    uint16_t avail_idx = blk->avail->idx;
    blk->avail->ring[avail_idx % VIRTQ_SIZE] = idx[0];

    /* 内存屏障 */
    __sync_synchronize();

    /* 更新可用环索引 */
    blk->avail->idx = avail_idx + 1;

    /* 内存屏障 */
    __sync_synchronize();

    /* 通知设备 */
    mmio_write32(blk->regs + VIRTIO_MMIO_QUEUE_NOTIFY/4, 0);

    /* 等待完成 */
    while (blk->used->idx == blk->last_used_idx) {
        /* 忙等待 */
        __sync_synchronize();
    }

    /* 处理已用描述符 */
    blk->last_used_idx++;

    /* 释放描述符链 */
    free_chain(blk, idx[0]);

    return (blk->status == VIRTIO_BLK_S_OK) ? 0 : -1;
}

int virtio_blk_read(virtio_blk_t *blk, size_t sector, uint8_t *buf) {
    return virtio_blk_rw(blk, sector, buf, 0);
}

int virtio_blk_write(virtio_blk_t *blk, size_t sector, const uint8_t *buf) {
    /* 需要临时缓冲区因为写操作不能使用 const 指针 */
    return virtio_blk_rw(blk, sector, (uint8_t *)buf, 1);
}

/* block_device_t 回调 */
static void bd_read_block(block_device_t *dev, size_t block_id, uint8_t *buf) {
    virtio_blk_t *blk = (virtio_blk_t *)dev->priv;
    virtio_blk_read(blk, block_id, buf);
}

static void bd_write_block(block_device_t *dev, size_t block_id, const uint8_t *buf) {
    virtio_blk_t *blk = (virtio_blk_t *)dev->priv;
    virtio_blk_write(blk, block_id, buf);
}

static block_device_t g_block_device;

block_device_t *virtio_blk_as_block_device(virtio_blk_t *blk) {
    g_block_device.read_block = bd_read_block;
    g_block_device.write_block = bd_write_block;
    g_block_device.priv = blk;
    return &g_block_device;
}
