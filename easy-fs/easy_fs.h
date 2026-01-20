/**
 * Easy File System (C 实现)
 *
 * 与 Rust 版本格式完全兼容
 */
#ifndef EASY_FS_H
#define EASY_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef long ssize_t;

/* ============================================================================
 * 常量定义
 * ========================================================================== */

#define BLOCK_SZ            512
#define EFS_MAGIC           0x3b800001
#define INODE_DIRECT_COUNT  28
#define NAME_LENGTH_LIMIT   27
#define DIRENT_SZ           32
#define BLOCK_BITS          (BLOCK_SZ * 8)
#define INODE_INDIRECT1_COUNT (BLOCK_SZ / 4)

/* ============================================================================
 * 块设备接口
 * ========================================================================== */

typedef struct block_device {
    void (*read_block)(struct block_device *dev, size_t block_id, uint8_t *buf);
    void (*write_block)(struct block_device *dev, size_t block_id, const uint8_t *buf);
    void *priv;
} block_device_t;

/* ============================================================================
 * 磁盘数据结构（与 Rust 版本完全兼容）
 * ========================================================================== */

/* 超级块 (24 字节) */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t total_blocks;
    uint32_t inode_bitmap_blocks;
    uint32_t inode_area_blocks;
    uint32_t data_bitmap_blocks;
    uint32_t data_area_blocks;
} super_block_t;

/* inode 类型 */
typedef enum {
    INODE_FILE = 0,
    INODE_DIRECTORY = 1,
} inode_type_t;

/* 磁盘 inode (128 字节) */
typedef struct __attribute__((packed)) {
    uint32_t size;
    uint32_t direct[INODE_DIRECT_COUNT];
    uint32_t indirect1;
    uint32_t indirect2;
    uint32_t type_;     /* inode_type_t */
} disk_inode_t;

/* 目录项 (32 字节) */
typedef struct __attribute__((packed)) {
    char name[NAME_LENGTH_LIMIT + 1];   /* 28 字节 */
    uint32_t inode_number;              /* 4 字节 */
} dir_entry_t;

/* ============================================================================
 * 内存数据结构
 * ========================================================================== */

/* 位图 */
typedef struct {
    size_t start_block_id;
    size_t blocks;
} bitmap_t;

/* 文件系统 */
typedef struct {
    block_device_t *block_device;
    bitmap_t inode_bitmap;
    bitmap_t data_bitmap;
    uint32_t inode_area_start_block;
    uint32_t data_area_start_block;
} easy_fs_t;

/* inode (内存表示) */
typedef struct {
    size_t block_id;
    size_t block_offset;
    easy_fs_t *fs;
} inode_t;

/* 文件句柄 */
typedef struct {
    inode_t *inode;
    bool readable;
    bool writable;
    size_t offset;
} file_handle_t;

/* 打开标志 */
#define O_RDONLY    0
#define O_WRONLY    (1 << 0)
#define O_RDWR      (1 << 1)
#define O_CREATE    (1 << 9)
#define O_TRUNC     (1 << 10)

/* ============================================================================
 * 块缓存
 * ========================================================================== */

#define BLOCK_CACHE_SIZE 16

typedef struct {
    uint8_t cache[BLOCK_SZ];
    size_t block_id;
    block_device_t *block_device;
    bool modified;
    bool valid;
} block_cache_t;

/* 初始化块缓存系统 */
void block_cache_init(void);

/* 获取块缓存 */
block_cache_t *get_block_cache(size_t block_id, block_device_t *dev);

/* 同步所有块缓存 */
void block_cache_sync_all(void);

/* 同步单个块缓存 */
void block_cache_sync(block_cache_t *cache);

/* ============================================================================
 * 文件系统 API
 * ========================================================================== */

/* 打开文件系统 */
easy_fs_t *efs_open(block_device_t *dev);

/* 获取根 inode */
inode_t *efs_root_inode(easy_fs_t *fs);

/* inode 操作 */
inode_t *inode_find(inode_t *dir, const char *name);
inode_t *inode_create(inode_t *dir, const char *name);
size_t inode_read_at(inode_t *inode, size_t offset, uint8_t *buf, size_t len);
size_t inode_write_at(inode_t *inode, size_t offset, const uint8_t *buf, size_t len);
void inode_clear(inode_t *inode);
size_t inode_readdir(inode_t *dir, char names[][NAME_LENGTH_LIMIT + 1], size_t max_count);
uint32_t inode_size(inode_t *inode);

/* 文件操作 */
file_handle_t *file_open(easy_fs_t *fs, const char *path, uint32_t flags);
void file_close(file_handle_t *fh);
ssize_t file_read(file_handle_t *fh, uint8_t *buf, size_t count);
ssize_t file_write(file_handle_t *fh, const uint8_t *buf, size_t count);

/* 辅助函数 */
void efs_get_disk_inode_pos(easy_fs_t *fs, uint32_t inode_id, uint32_t *block_id, size_t *offset);
uint32_t efs_alloc_inode(easy_fs_t *fs);
uint32_t efs_alloc_data(easy_fs_t *fs);
void efs_dealloc_data(easy_fs_t *fs, uint32_t block_id);

#endif /* EASY_FS_H */
