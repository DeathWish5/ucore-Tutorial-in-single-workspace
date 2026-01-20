/**
 * Easy File System 实现
 */
#include "easy_fs.h"
#include "../kernel-alloc/heap.h"
#include <string.h>

/* ============================================================================
 * 块缓存
 * ========================================================================== */

static block_cache_t g_block_cache[BLOCK_CACHE_SIZE];

void block_cache_init(void) {
    memset(g_block_cache, 0, sizeof(g_block_cache));
}

void block_cache_sync(block_cache_t *cache) {
    if (cache->valid && cache->modified) {
        cache->block_device->write_block(cache->block_device, cache->block_id, cache->cache);
        cache->modified = false;
    }
}

void block_cache_sync_all(void) {
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        block_cache_sync(&g_block_cache[i]);
    }
}

block_cache_t *get_block_cache(size_t block_id, block_device_t *dev) {
    /* 查找已有缓存 */
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (g_block_cache[i].valid &&
            g_block_cache[i].block_id == block_id &&
            g_block_cache[i].block_device == dev) {
            return &g_block_cache[i];
        }
    }

    /* 查找空闲槽位 */
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (!g_block_cache[i].valid) {
            g_block_cache[i].block_id = block_id;
            g_block_cache[i].block_device = dev;
            g_block_cache[i].modified = false;
            g_block_cache[i].valid = true;
            dev->read_block(dev, block_id, g_block_cache[i].cache);
            return &g_block_cache[i];
        }
    }

    /* 替换第一个缓存（简单策略） */
    block_cache_sync(&g_block_cache[0]);
    g_block_cache[0].block_id = block_id;
    g_block_cache[0].block_device = dev;
    g_block_cache[0].modified = false;
    dev->read_block(dev, block_id, g_block_cache[0].cache);
    return &g_block_cache[0];
}

/* ============================================================================
 * 位图操作
 * ========================================================================== */

/* 计算尾部零的个数 */
static int ctz64(uint64_t x) {
    if (x == 0) return 64;
    int n = 0;
    if ((x & 0xFFFFFFFF) == 0) { n += 32; x >>= 32; }
    if ((x & 0xFFFF) == 0) { n += 16; x >>= 16; }
    if ((x & 0xFF) == 0) { n += 8; x >>= 8; }
    if ((x & 0xF) == 0) { n += 4; x >>= 4; }
    if ((x & 0x3) == 0) { n += 2; x >>= 2; }
    if ((x & 0x1) == 0) { n += 1; }
    return n;
}

static int bitmap_alloc(bitmap_t *bm, block_device_t *dev) {
    for (size_t block_id = 0; block_id < bm->blocks; block_id++) {
        block_cache_t *cache = get_block_cache(block_id + bm->start_block_id, dev);
        uint64_t *bitmap_block = (uint64_t *)cache->cache;

        for (size_t bits64_pos = 0; bits64_pos < 64; bits64_pos++) {
            if (bitmap_block[bits64_pos] != ~0ULL) {
                /* 找到空闲位 */
                int inner_pos = ctz64(~bitmap_block[bits64_pos]);
                bitmap_block[bits64_pos] |= (1ULL << inner_pos);
                cache->modified = true;
                return block_id * BLOCK_BITS + bits64_pos * 64 + inner_pos;
            }
        }
    }
    return -1;
}

static void bitmap_dealloc(bitmap_t *bm, block_device_t *dev, size_t bit) {
    size_t block_pos = bit / BLOCK_BITS;
    size_t bits64_pos = (bit % BLOCK_BITS) / 64;
    size_t inner_pos = bit % 64;

    block_cache_t *cache = get_block_cache(block_pos + bm->start_block_id, dev);
    uint64_t *bitmap_block = (uint64_t *)cache->cache;
    bitmap_block[bits64_pos] &= ~(1ULL << inner_pos);
    cache->modified = true;
}

/* ============================================================================
 * DiskInode 操作
 * ========================================================================== */

static uint32_t disk_inode_data_blocks(uint32_t size) {
    return (size + BLOCK_SZ - 1) / BLOCK_SZ;
}

static uint32_t disk_inode_get_block_id(disk_inode_t *di, uint32_t inner_id, block_device_t *dev) {
    if (inner_id < INODE_DIRECT_COUNT) {
        return di->direct[inner_id];
    } else if (inner_id < INODE_DIRECT_COUNT + INODE_INDIRECT1_COUNT) {
        block_cache_t *cache = get_block_cache(di->indirect1, dev);
        uint32_t *indirect_block = (uint32_t *)cache->cache;
        return indirect_block[inner_id - INODE_DIRECT_COUNT];
    } else {
        size_t last = inner_id - INODE_DIRECT_COUNT - INODE_INDIRECT1_COUNT;
        block_cache_t *cache2 = get_block_cache(di->indirect2, dev);
        uint32_t *indirect2 = (uint32_t *)cache2->cache;
        uint32_t indirect1_id = indirect2[last / INODE_INDIRECT1_COUNT];
        block_cache_t *cache1 = get_block_cache(indirect1_id, dev);
        uint32_t *indirect1 = (uint32_t *)cache1->cache;
        return indirect1[last % INODE_INDIRECT1_COUNT];
    }
}

static size_t disk_inode_read_at(disk_inode_t *di, size_t offset, uint8_t *buf, size_t len, block_device_t *dev) {
    size_t start = offset;
    size_t end = offset + len;
    if (end > di->size) end = di->size;
    if (start >= end) return 0;

    size_t start_block = start / BLOCK_SZ;
    size_t read_size = 0;

    while (start < end) {
        size_t end_current_block = (start / BLOCK_SZ + 1) * BLOCK_SZ;
        if (end_current_block > end) end_current_block = end;

        size_t block_read_size = end_current_block - start;
        uint32_t block_id = disk_inode_get_block_id(di, start_block, dev);
        block_cache_t *cache = get_block_cache(block_id, dev);

        memcpy(buf + read_size, cache->cache + (start % BLOCK_SZ), block_read_size);
        read_size += block_read_size;

        if (end_current_block == end) break;
        start_block++;
        start = end_current_block;
    }

    return read_size;
}

static size_t disk_inode_write_at(disk_inode_t *di, size_t offset, const uint8_t *buf, size_t len, block_device_t *dev) {
    size_t start = offset;
    size_t end = offset + len;
    if (end > di->size) end = di->size;
    if (start >= end) return 0;

    size_t start_block = start / BLOCK_SZ;
    size_t write_size = 0;

    while (start < end) {
        size_t end_current_block = (start / BLOCK_SZ + 1) * BLOCK_SZ;
        if (end_current_block > end) end_current_block = end;

        size_t block_write_size = end_current_block - start;
        uint32_t block_id = disk_inode_get_block_id(di, start_block, dev);
        block_cache_t *cache = get_block_cache(block_id, dev);

        memcpy(cache->cache + (start % BLOCK_SZ), buf + write_size, block_write_size);
        cache->modified = true;
        write_size += block_write_size;

        if (end_current_block == end) break;
        start_block++;
        start = end_current_block;
    }

    return write_size;
}

/* 增加 inode 大小 */
static void disk_inode_increase_size(disk_inode_t *di, uint32_t new_size, easy_fs_t *fs) {
    if (new_size <= di->size) return;

    uint32_t old_blocks = disk_inode_data_blocks(di->size);
    uint32_t new_blocks = disk_inode_data_blocks(new_size);
    di->size = new_size;

    uint32_t current = old_blocks;
    block_device_t *dev = fs->block_device;

    /* 填充 direct */
    while (current < new_blocks && current < INODE_DIRECT_COUNT) {
        di->direct[current] = efs_alloc_data(fs);
        current++;
    }

    if (current >= new_blocks) return;

    /* 分配 indirect1 */
    if (old_blocks < INODE_DIRECT_COUNT && new_blocks > INODE_DIRECT_COUNT) {
        di->indirect1 = efs_alloc_data(fs);
    }

    /* 填充 indirect1 */
    if (current < INODE_DIRECT_COUNT + INODE_INDIRECT1_COUNT) {
        block_cache_t *cache = get_block_cache(di->indirect1, dev);
        uint32_t *indirect1 = (uint32_t *)cache->cache;

        while (current < new_blocks && current < INODE_DIRECT_COUNT + INODE_INDIRECT1_COUNT) {
            indirect1[current - INODE_DIRECT_COUNT] = efs_alloc_data(fs);
            cache->modified = true;
            current++;
        }
    }

    /* indirect2 略（大文件支持） */
}

/* ============================================================================
 * 文件系统操作
 * ========================================================================== */

void efs_get_disk_inode_pos(easy_fs_t *fs, uint32_t inode_id, uint32_t *block_id, size_t *offset) {
    size_t inode_size = sizeof(disk_inode_t);
    size_t inodes_per_block = BLOCK_SZ / inode_size;
    *block_id = fs->inode_area_start_block + inode_id / inodes_per_block;
    *offset = (inode_id % inodes_per_block) * inode_size;
}

uint32_t efs_alloc_inode(easy_fs_t *fs) {
    return bitmap_alloc(&fs->inode_bitmap, fs->block_device);
}

uint32_t efs_alloc_data(easy_fs_t *fs) {
    int bit = bitmap_alloc(&fs->data_bitmap, fs->block_device);
    return fs->data_area_start_block + bit;
}

void efs_dealloc_data(easy_fs_t *fs, uint32_t block_id) {
    /* 清零块 */
    block_cache_t *cache = get_block_cache(block_id, fs->block_device);
    memset(cache->cache, 0, BLOCK_SZ);
    cache->modified = true;
    /* 释放位图 */
    bitmap_dealloc(&fs->data_bitmap, fs->block_device, block_id - fs->data_area_start_block);
}

easy_fs_t *efs_open(block_device_t *dev) {
    block_cache_t *cache = get_block_cache(0, dev);
    super_block_t *sb = (super_block_t *)cache->cache;

    if (sb->magic != EFS_MAGIC) {
        return NULL;
    }

    easy_fs_t *fs = heap_alloc(sizeof(easy_fs_t), 8);
    if (!fs) return NULL;

    fs->block_device = dev;

    fs->inode_bitmap.start_block_id = 1;
    fs->inode_bitmap.blocks = sb->inode_bitmap_blocks;

    uint32_t inode_total_blocks = sb->inode_bitmap_blocks + sb->inode_area_blocks;
    fs->data_bitmap.start_block_id = 1 + inode_total_blocks;
    fs->data_bitmap.blocks = sb->data_bitmap_blocks;

    fs->inode_area_start_block = 1 + sb->inode_bitmap_blocks;
    fs->data_area_start_block = 1 + inode_total_blocks + sb->data_bitmap_blocks;

    return fs;
}

inode_t *efs_root_inode(easy_fs_t *fs) {
    inode_t *inode = heap_alloc(sizeof(inode_t), 8);
    if (!inode) return NULL;

    uint32_t block_id;
    size_t offset;
    efs_get_disk_inode_pos(fs, 0, &block_id, &offset);

    inode->block_id = block_id;
    inode->block_offset = offset;
    inode->fs = fs;

    return inode;
}

/* ============================================================================
 * Inode 操作
 * ========================================================================== */

static disk_inode_t *inode_get_disk_inode(inode_t *inode) {
    block_cache_t *cache = get_block_cache(inode->block_id, inode->fs->block_device);
    return (disk_inode_t *)(cache->cache + inode->block_offset);
}

static void inode_mark_modified(inode_t *inode) {
    block_cache_t *cache = get_block_cache(inode->block_id, inode->fs->block_device);
    cache->modified = true;
}

static int inode_find_inode_id(inode_t *dir, const char *name, disk_inode_t *di) {
    if (di->type_ != INODE_DIRECTORY) return -1;

    size_t file_count = di->size / DIRENT_SZ;
    dir_entry_t dirent;

    for (size_t i = 0; i < file_count; i++) {
        disk_inode_read_at(di, i * DIRENT_SZ, (uint8_t *)&dirent, DIRENT_SZ, dir->fs->block_device);
        if (strcmp(dirent.name, name) == 0) {
            return dirent.inode_number;
        }
    }
    return -1;
}

inode_t *inode_find(inode_t *dir, const char *name) {
    disk_inode_t *di = inode_get_disk_inode(dir);
    int inode_id = inode_find_inode_id(dir, name, di);
    if (inode_id < 0) return NULL;

    inode_t *inode = heap_alloc(sizeof(inode_t), 8);
    if (!inode) return NULL;

    uint32_t block_id;
    size_t offset;
    efs_get_disk_inode_pos(dir->fs, inode_id, &block_id, &offset);

    inode->block_id = block_id;
    inode->block_offset = offset;
    inode->fs = dir->fs;

    return inode;
}

inode_t *inode_create(inode_t *dir, const char *name) {
    easy_fs_t *fs = dir->fs;

    /* 分配新 inode */
    uint32_t new_inode_id = efs_alloc_inode(fs);

    /* 初始化新 inode */
    uint32_t new_block_id;
    size_t new_offset;
    efs_get_disk_inode_pos(fs, new_inode_id, &new_block_id, &new_offset);

    block_cache_t *new_cache = get_block_cache(new_block_id, fs->block_device);
    disk_inode_t *new_di = (disk_inode_t *)(new_cache->cache + new_offset);
    memset(new_di, 0, sizeof(disk_inode_t));
    new_di->type_ = INODE_FILE;
    new_cache->modified = true;

    /* 在目录中添加目录项 */
    disk_inode_t *dir_di = inode_get_disk_inode(dir);
    size_t file_count = dir_di->size / DIRENT_SZ;
    uint32_t new_size = (file_count + 1) * DIRENT_SZ;

    disk_inode_increase_size(dir_di, new_size, fs);
    inode_mark_modified(dir);

    /* 写目录项 */
    dir_entry_t dirent;
    memset(&dirent, 0, sizeof(dirent));
    strncpy(dirent.name, name, NAME_LENGTH_LIMIT);
    dirent.inode_number = new_inode_id;

    disk_inode_write_at(dir_di, file_count * DIRENT_SZ, (uint8_t *)&dirent, DIRENT_SZ, fs->block_device);

    block_cache_sync_all();

    /* 返回新 inode */
    inode_t *inode = heap_alloc(sizeof(inode_t), 8);
    if (!inode) return NULL;

    inode->block_id = new_block_id;
    inode->block_offset = new_offset;
    inode->fs = fs;

    return inode;
}

size_t inode_read_at(inode_t *inode, size_t offset, uint8_t *buf, size_t len) {
    disk_inode_t *di = inode_get_disk_inode(inode);
    return disk_inode_read_at(di, offset, buf, len, inode->fs->block_device);
}

size_t inode_write_at(inode_t *inode, size_t offset, const uint8_t *buf, size_t len) {
    disk_inode_t *di = inode_get_disk_inode(inode);
    uint32_t new_size = offset + len;
    if (new_size > di->size) {
        disk_inode_increase_size(di, new_size, inode->fs);
        inode_mark_modified(inode);
    }
    size_t result = disk_inode_write_at(di, offset, buf, len, inode->fs->block_device);
    block_cache_sync_all();
    return result;
}

void inode_clear(inode_t *inode) {
    disk_inode_t *di = inode_get_disk_inode(inode);
    easy_fs_t *fs = inode->fs;

    /* 释放数据块 */
    uint32_t data_blocks = disk_inode_data_blocks(di->size);
    for (uint32_t i = 0; i < data_blocks && i < INODE_DIRECT_COUNT; i++) {
        if (di->direct[i] != 0) {
            efs_dealloc_data(fs, di->direct[i]);
            di->direct[i] = 0;
        }
    }

    /* 处理 indirect1 */
    if (di->indirect1 != 0 && data_blocks > INODE_DIRECT_COUNT) {
        block_cache_t *cache = get_block_cache(di->indirect1, fs->block_device);
        uint32_t *indirect1 = (uint32_t *)cache->cache;
        for (uint32_t i = 0; i < data_blocks - INODE_DIRECT_COUNT && i < INODE_INDIRECT1_COUNT; i++) {
            if (indirect1[i] != 0) {
                efs_dealloc_data(fs, indirect1[i]);
            }
        }
        efs_dealloc_data(fs, di->indirect1);
        di->indirect1 = 0;
    }

    di->size = 0;
    inode_mark_modified(inode);
    block_cache_sync_all();
}

size_t inode_readdir(inode_t *dir, char names[][NAME_LENGTH_LIMIT + 1], size_t max_count) {
    disk_inode_t *di = inode_get_disk_inode(dir);
    size_t file_count = di->size / DIRENT_SZ;
    if (file_count > max_count) file_count = max_count;

    dir_entry_t dirent;
    for (size_t i = 0; i < file_count; i++) {
        disk_inode_read_at(di, i * DIRENT_SZ, (uint8_t *)&dirent, DIRENT_SZ, dir->fs->block_device);
        strncpy(names[i], dirent.name, NAME_LENGTH_LIMIT);
        names[i][NAME_LENGTH_LIMIT] = '\0';
    }

    return file_count;
}

uint32_t inode_size(inode_t *inode) {
    disk_inode_t *di = inode_get_disk_inode(inode);
    return di->size;
}

/* ============================================================================
 * 文件操作
 * ========================================================================== */

file_handle_t *file_open(easy_fs_t *fs, const char *path, uint32_t flags) {
    inode_t *root = efs_root_inode(fs);
    if (!root) return NULL;

    bool readable = (flags == O_RDONLY) || (flags & O_RDWR);
    bool writable = (flags & O_WRONLY) || (flags & O_RDWR);

    inode_t *inode = inode_find(root, path);

    if (flags & O_CREATE) {
        if (inode) {
            /* 已存在，清空 */
            inode_clear(inode);
        } else {
            /* 创建新文件 */
            inode = inode_create(root, path);
        }
    } else {
        if (!inode) {
            heap_free(root, sizeof(inode_t));
            return NULL;
        }
        if (flags & O_TRUNC) {
            inode_clear(inode);
        }
    }

    heap_free(root, sizeof(inode_t));

    if (!inode) return NULL;

    file_handle_t *fh = heap_alloc(sizeof(file_handle_t), 8);
    if (!fh) {
        heap_free(inode, sizeof(inode_t));
        return NULL;
    }

    fh->inode = inode;
    fh->readable = readable;
    fh->writable = writable;
    fh->offset = 0;

    return fh;
}

void file_close(file_handle_t *fh) {
    if (fh) {
        if (fh->inode) {
            heap_free(fh->inode, sizeof(inode_t));
        }
        heap_free(fh, sizeof(file_handle_t));
    }
}

ssize_t file_read(file_handle_t *fh, uint8_t *buf, size_t count) {
    if (!fh || !fh->readable || !fh->inode) return -1;

    size_t read = inode_read_at(fh->inode, fh->offset, buf, count);
    fh->offset += read;
    return read;
}

ssize_t file_write(file_handle_t *fh, const uint8_t *buf, size_t count) {
    if (!fh || !fh->writable || !fh->inode) return -1;

    size_t written = inode_write_at(fh->inode, fh->offset, buf, count);
    fh->offset += written;
    return written;
}
