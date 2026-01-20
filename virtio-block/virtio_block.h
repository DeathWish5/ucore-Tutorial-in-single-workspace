/**
 * VirtIO Block 设备驱动
 *
 * 简化实现，用于 QEMU virt 平台
 */
#ifndef VIRTIO_BLOCK_H
#define VIRTIO_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../easy-fs/easy_fs.h"

/* QEMU virt 平台的 virtio 设备地址 */
#define VIRTIO0_BASE 0x10001000

/* VirtIO MMIO 寄存器偏移 */
#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_VENDOR_ID           0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_ALIGN         0x03c
#define VIRTIO_MMIO_QUEUE_PFN           0x040
#define VIRTIO_MMIO_QUEUE_READY         0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0a4

/* VirtIO 状态位 */
#define VIRTIO_STATUS_ACKNOWLEDGE       1
#define VIRTIO_STATUS_DRIVER            2
#define VIRTIO_STATUS_DRIVER_OK         4
#define VIRTIO_STATUS_FEATURES_OK       8

/* VirtIO Block 请求类型 */
#define VIRTIO_BLK_T_IN     0   /* 读 */
#define VIRTIO_BLK_T_OUT    1   /* 写 */

/* VirtIO Block 状态 */
#define VIRTIO_BLK_S_OK     0
#define VIRTIO_BLK_S_IOERR  1

/* VirtQueue 描述符标志 */
#define VIRTQ_DESC_F_NEXT     1
#define VIRTQ_DESC_F_WRITE    2

/* VirtQueue 大小 */
#define VIRTQ_SIZE 8

/* VirtQueue 描述符 */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

/* VirtQueue 可用环 */
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_SIZE];
    uint16_t used_event;
} virtq_avail_t;

/* VirtQueue 已用元素 */
typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

/* VirtQueue 已用环 */
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTQ_SIZE];
    uint16_t avail_event;
} virtq_used_t;

/* VirtIO Block 请求头 */
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} virtio_blk_req_t;

/* VirtIO Block 设备上下文 */
typedef struct {
    volatile uint32_t *regs;
    virtq_desc_t *desc;
    virtq_avail_t *avail;
    virtq_used_t *used;
    uint16_t last_used_idx;
    bool free[VIRTQ_SIZE];
    /* 请求相关 */
    virtio_blk_req_t req;
    uint8_t status;
} virtio_blk_t;

/* 初始化 VirtIO Block 设备 */
int virtio_blk_init(virtio_blk_t *blk);

/* 读块 */
int virtio_blk_read(virtio_blk_t *blk, size_t sector, uint8_t *buf);

/* 写块 */
int virtio_blk_write(virtio_blk_t *blk, size_t sector, const uint8_t *buf);

/* 获取作为 block_device_t 的接口 */
block_device_t *virtio_blk_as_block_device(virtio_blk_t *blk);

#endif /* VIRTIO_BLOCK_H */
