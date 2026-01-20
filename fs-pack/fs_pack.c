/**
 * Easy File System 打包工具
 *
 * 用于在主机端创建 fs.img 文件系统镜像
 * 与 Rust 版本格式完全兼容
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* ============================================================================
 * 常量定义（与内核端 easy_fs.h 保持一致）
 * ========================================================================== */

#define BLOCK_SZ            512
#define EFS_MAGIC           0x3b800001
#define INODE_DIRECT_COUNT  28
#define NAME_LENGTH_LIMIT   27
#define DIRENT_SZ           32
#define BLOCK_BITS          (BLOCK_SZ * 8)
#define INODE_INDIRECT1_COUNT (BLOCK_SZ / 4)

/* 默认参数 */
#define DEFAULT_TOTAL_BLOCKS    (64 * 2048)     /* 64MB */
#define DEFAULT_INODE_BITMAP_BLOCKS 1

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
    uint32_t type_;
} disk_inode_t;

/* 目录项 (32 字节) */
typedef struct __attribute__((packed)) {
    char name[NAME_LENGTH_LIMIT + 1];
    uint32_t inode_number;
} dir_entry_t;

/* ============================================================================
 * 块设备（基于文件）
 * ========================================================================== */

typedef struct {
    FILE *file;
} block_file_t;

static void block_read(block_file_t *dev, size_t block_id, uint8_t *buf) {
    fseek(dev->file, block_id * BLOCK_SZ, SEEK_SET);
    size_t n = fread(buf, 1, BLOCK_SZ, dev->file);
    if (n != BLOCK_SZ) {
        fprintf(stderr, "Error: Failed to read block %zu\n", block_id);
        exit(1);
    }
}

static void block_write(block_file_t *dev, size_t block_id, const uint8_t *buf) {
    fseek(dev->file, block_id * BLOCK_SZ, SEEK_SET);
    size_t n = fwrite(buf, 1, BLOCK_SZ, dev->file);
    if (n != BLOCK_SZ) {
        fprintf(stderr, "Error: Failed to write block %zu\n", block_id);
        exit(1);
    }
}

/* ============================================================================
 * 简单块缓存
 * ========================================================================== */

#define BLOCK_CACHE_SIZE 256

typedef struct {
    uint8_t cache[BLOCK_SZ];
    size_t block_id;
    block_file_t *dev;
    bool modified;
    bool valid;
} block_cache_t;

static block_cache_t g_block_cache[BLOCK_CACHE_SIZE];

static void block_cache_init(void) {
    memset(g_block_cache, 0, sizeof(g_block_cache));
}

static void block_cache_sync(block_cache_t *cache) {
    if (cache->valid && cache->modified) {
        block_write(cache->dev, cache->block_id, cache->cache);
        cache->modified = false;
    }
}

static void block_cache_sync_all(void) {
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        block_cache_sync(&g_block_cache[i]);
    }
}

static block_cache_t *get_block_cache(size_t block_id, block_file_t *dev) {
    /* 查找已有缓存 */
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (g_block_cache[i].valid &&
            g_block_cache[i].block_id == block_id &&
            g_block_cache[i].dev == dev) {
            return &g_block_cache[i];
        }
    }

    /* 查找空闲槽位 */
    for (int i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (!g_block_cache[i].valid) {
            g_block_cache[i].block_id = block_id;
            g_block_cache[i].dev = dev;
            g_block_cache[i].modified = false;
            g_block_cache[i].valid = true;
            block_read(dev, block_id, g_block_cache[i].cache);
            return &g_block_cache[i];
        }
    }

    /* 替换第一个缓存 */
    block_cache_sync(&g_block_cache[0]);
    g_block_cache[0].block_id = block_id;
    g_block_cache[0].dev = dev;
    g_block_cache[0].modified = false;
    block_read(dev, block_id, g_block_cache[0].cache);
    return &g_block_cache[0];
}

/* ============================================================================
 * 位图操作
 * ========================================================================== */

typedef struct {
    size_t start_block_id;
    size_t blocks;
} bitmap_t;

static int bitmap_alloc(bitmap_t *bm, block_file_t *dev) {
    for (size_t block_id = 0; block_id < bm->blocks; block_id++) {
        block_cache_t *cache = get_block_cache(block_id + bm->start_block_id, dev);
        uint64_t *bitmap_block = (uint64_t *)cache->cache;

        for (size_t bits64_pos = 0; bits64_pos < 64; bits64_pos++) {
            if (bitmap_block[bits64_pos] != ~0ULL) {
                int inner_pos = __builtin_ctzll(~bitmap_block[bits64_pos]);
                bitmap_block[bits64_pos] |= (1ULL << inner_pos);
                cache->modified = true;
                return block_id * BLOCK_BITS + bits64_pos * 64 + inner_pos;
            }
        }
    }
    return -1;
}

static size_t bitmap_maximum(bitmap_t *bm) {
    return bm->blocks * BLOCK_BITS;
}

/* ============================================================================
 * 文件系统结构
 * ========================================================================== */

typedef struct {
    block_file_t *dev;
    bitmap_t inode_bitmap;
    bitmap_t data_bitmap;
    uint32_t inode_area_start_block;
    uint32_t data_area_start_block;
} easy_fs_t;

static void efs_get_disk_inode_pos(easy_fs_t *fs, uint32_t inode_id,
                                   uint32_t *block_id, size_t *offset) {
    size_t inode_size = sizeof(disk_inode_t);
    size_t inodes_per_block = BLOCK_SZ / inode_size;
    *block_id = fs->inode_area_start_block + inode_id / inodes_per_block;
    *offset = (inode_id % inodes_per_block) * inode_size;
}

static uint32_t efs_alloc_inode(easy_fs_t *fs) {
    return bitmap_alloc(&fs->inode_bitmap, fs->dev);
}

static uint32_t efs_alloc_data(easy_fs_t *fs) {
    int bit = bitmap_alloc(&fs->data_bitmap, fs->dev);
    return fs->data_area_start_block + bit;
}

/* ============================================================================
 * 创建文件系统
 * ========================================================================== */

static easy_fs_t *efs_create(block_file_t *dev, uint32_t total_blocks,
                             uint32_t inode_bitmap_blocks) {
    easy_fs_t *fs = malloc(sizeof(easy_fs_t));
    if (!fs) return NULL;

    fs->dev = dev;

    /* 计算各区域大小 */
    fs->inode_bitmap.start_block_id = 1;
    fs->inode_bitmap.blocks = inode_bitmap_blocks;

    size_t inode_num = bitmap_maximum(&fs->inode_bitmap);
    uint32_t inode_area_blocks = (inode_num * sizeof(disk_inode_t) + BLOCK_SZ - 1) / BLOCK_SZ;
    uint32_t inode_total_blocks = inode_bitmap_blocks + inode_area_blocks;

    uint32_t data_total_blocks = total_blocks - 1 - inode_total_blocks;
    uint32_t data_bitmap_blocks = (data_total_blocks + 4096) / 4097;
    uint32_t data_area_blocks = data_total_blocks - data_bitmap_blocks;

    fs->data_bitmap.start_block_id = 1 + inode_bitmap_blocks + inode_area_blocks;
    fs->data_bitmap.blocks = data_bitmap_blocks;

    fs->inode_area_start_block = 1 + inode_bitmap_blocks;
    fs->data_area_start_block = 1 + inode_total_blocks + data_bitmap_blocks;

    /* 清零所有块 */
    printf("Clearing %u blocks...\n", total_blocks);
    uint8_t zero_block[BLOCK_SZ];
    memset(zero_block, 0, BLOCK_SZ);
    for (uint32_t i = 0; i < total_blocks; i++) {
        block_write(dev, i, zero_block);
    }

    /* 初始化超级块 */
    printf("Initializing super block...\n");
    block_cache_t *sb_cache = get_block_cache(0, dev);
    super_block_t *sb = (super_block_t *)sb_cache->cache;
    sb->magic = EFS_MAGIC;
    sb->total_blocks = total_blocks;
    sb->inode_bitmap_blocks = inode_bitmap_blocks;
    sb->inode_area_blocks = inode_area_blocks;
    sb->data_bitmap_blocks = data_bitmap_blocks;
    sb->data_area_blocks = data_area_blocks;
    sb_cache->modified = true;

    printf("  inode_bitmap_blocks: %u\n", inode_bitmap_blocks);
    printf("  inode_area_blocks: %u\n", inode_area_blocks);
    printf("  data_bitmap_blocks: %u\n", data_bitmap_blocks);
    printf("  data_area_blocks: %u\n", data_area_blocks);

    /* 创建根目录 inode */
    printf("Creating root inode...\n");
    uint32_t root_inode_id = efs_alloc_inode(fs);
    if (root_inode_id != 0) {
        fprintf(stderr, "Error: Root inode should be 0, got %u\n", root_inode_id);
        free(fs);
        return NULL;
    }

    uint32_t root_block_id;
    size_t root_offset;
    efs_get_disk_inode_pos(fs, 0, &root_block_id, &root_offset);

    block_cache_t *root_cache = get_block_cache(root_block_id, dev);
    disk_inode_t *root_di = (disk_inode_t *)(root_cache->cache + root_offset);
    memset(root_di, 0, sizeof(disk_inode_t));
    root_di->type_ = INODE_DIRECTORY;
    root_cache->modified = true;

    block_cache_sync_all();

    return fs;
}

/* ============================================================================
 * Inode 操作
 * ========================================================================== */

typedef struct {
    uint32_t block_id;
    size_t block_offset;
    easy_fs_t *fs;
} inode_t;

static disk_inode_t *inode_get_disk_inode(inode_t *inode) {
    block_cache_t *cache = get_block_cache(inode->block_id, inode->fs->dev);
    return (disk_inode_t *)(cache->cache + inode->block_offset);
}

static void inode_mark_modified(inode_t *inode) {
    block_cache_t *cache = get_block_cache(inode->block_id, inode->fs->dev);
    cache->modified = true;
}

static uint32_t disk_inode_data_blocks(uint32_t size) {
    return (size + BLOCK_SZ - 1) / BLOCK_SZ;
}

static uint32_t disk_inode_get_block_id(disk_inode_t *di, uint32_t inner_id, block_file_t *dev) {
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

static void disk_inode_increase_size(disk_inode_t *di, uint32_t new_size, easy_fs_t *fs) {
    if (new_size <= di->size) return;

    uint32_t old_blocks = disk_inode_data_blocks(di->size);
    uint32_t new_blocks = disk_inode_data_blocks(new_size);
    di->size = new_size;

    uint32_t current = old_blocks;

    /* 填充 direct */
    while (current < new_blocks && current < INODE_DIRECT_COUNT) {
        di->direct[current] = efs_alloc_data(fs);
        current++;
    }

    if (current >= new_blocks) return;

    /* 分配 indirect1 */
    if (old_blocks <= INODE_DIRECT_COUNT && new_blocks > INODE_DIRECT_COUNT) {
        di->indirect1 = efs_alloc_data(fs);
    }

    /* 填充 indirect1 */
    if (current < INODE_DIRECT_COUNT + INODE_INDIRECT1_COUNT) {
        block_cache_t *cache = get_block_cache(di->indirect1, fs->dev);
        uint32_t *indirect1 = (uint32_t *)cache->cache;

        while (current < new_blocks && current < INODE_DIRECT_COUNT + INODE_INDIRECT1_COUNT) {
            indirect1[current - INODE_DIRECT_COUNT] = efs_alloc_data(fs);
            cache->modified = true;
            current++;
        }
    }

    if (current >= new_blocks) return;

    /* 分配 indirect2 */
    if (old_blocks <= INODE_DIRECT_COUNT + INODE_INDIRECT1_COUNT &&
        new_blocks > INODE_DIRECT_COUNT + INODE_INDIRECT1_COUNT) {
        di->indirect2 = efs_alloc_data(fs);
    }

    /* 填充 indirect2 */
    block_cache_t *cache2 = get_block_cache(di->indirect2, fs->dev);
    uint32_t *indirect2 = (uint32_t *)cache2->cache;

    while (current < new_blocks) {
        size_t idx = current - INODE_DIRECT_COUNT - INODE_INDIRECT1_COUNT;
        size_t a = idx / INODE_INDIRECT1_COUNT;
        size_t b = idx % INODE_INDIRECT1_COUNT;

        if (b == 0) {
            indirect2[a] = efs_alloc_data(fs);
            cache2->modified = true;
        }

        block_cache_t *cache1 = get_block_cache(indirect2[a], fs->dev);
        uint32_t *indirect1 = (uint32_t *)cache1->cache;
        indirect1[b] = efs_alloc_data(fs);
        cache1->modified = true;

        current++;
    }
}

static size_t disk_inode_write_at(disk_inode_t *di, size_t offset, const uint8_t *buf,
                                  size_t len, block_file_t *dev) {
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

static size_t disk_inode_read_at(disk_inode_t *di, size_t offset, uint8_t *buf,
                                 size_t len, block_file_t *dev) {
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

static inode_t *efs_root_inode(easy_fs_t *fs) {
    inode_t *inode = malloc(sizeof(inode_t));
    if (!inode) return NULL;

    uint32_t block_id;
    size_t offset;
    efs_get_disk_inode_pos(fs, 0, &block_id, &offset);

    inode->block_id = block_id;
    inode->block_offset = offset;
    inode->fs = fs;

    return inode;
}

static inode_t *inode_create(inode_t *dir, const char *name) {
    easy_fs_t *fs = dir->fs;

    /* 分配新 inode */
    uint32_t new_inode_id = efs_alloc_inode(fs);

    /* 初始化新 inode */
    uint32_t new_block_id;
    size_t new_offset;
    efs_get_disk_inode_pos(fs, new_inode_id, &new_block_id, &new_offset);

    block_cache_t *new_cache = get_block_cache(new_block_id, fs->dev);
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

    disk_inode_write_at(dir_di, file_count * DIRENT_SZ, (uint8_t *)&dirent, DIRENT_SZ, fs->dev);

    block_cache_sync_all();

    /* 返回新 inode */
    inode_t *inode = malloc(sizeof(inode_t));
    if (!inode) return NULL;

    inode->block_id = new_block_id;
    inode->block_offset = new_offset;
    inode->fs = fs;

    return inode;
}

static size_t inode_write_at(inode_t *inode, size_t offset, const uint8_t *buf, size_t len) {
    disk_inode_t *di = inode_get_disk_inode(inode);
    uint32_t new_size = offset + len;
    if (new_size > di->size) {
        disk_inode_increase_size(di, new_size, inode->fs);
        inode_mark_modified(inode);
        /* 重新获取 di，因为缓存可能在 increase_size 期间被替换 */
        di = inode_get_disk_inode(inode);
    }
    size_t result = disk_inode_write_at(di, offset, buf, len, inode->fs->dev);
    block_cache_sync_all();
    return result;
}

/* 读取目录 */
static size_t inode_readdir(inode_t *dir, char names[][NAME_LENGTH_LIMIT + 1], size_t max_count) {
    disk_inode_t *di = inode_get_disk_inode(dir);
    size_t file_count = di->size / DIRENT_SZ;
    if (file_count > max_count) file_count = max_count;

    dir_entry_t dirent;
    for (size_t i = 0; i < file_count; i++) {
        disk_inode_read_at(di, i * DIRENT_SZ, (uint8_t *)&dirent, DIRENT_SZ, dir->fs->dev);
        strncpy(names[i], dirent.name, NAME_LENGTH_LIMIT);
        names[i][NAME_LENGTH_LIMIT] = '\0';
    }

    return file_count;
}

/* ============================================================================
 * 主程序
 * ========================================================================== */

static void print_usage(const char *prog) {
    printf("Usage: %s <output_img> <input_dir> [file1] [file2] ...\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  <output_img>  Output fs.img path\n");
    printf("  <input_dir>   Directory containing ELF files\n");
    printf("  [files...]    Files to pack (if not specified, pack all files in input_dir)\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s build/fs.img ../user/build 00hello_world initproc user_shell\n", prog);
}

static uint8_t *read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = malloc(size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *size_out = size;
    return data;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *output_img = argv[1];
    const char *input_dir = argv[2];

    printf("=== Easy File System Packer ===\n");
    printf("Output: %s\n", output_img);
    printf("Input dir: %s\n", input_dir);

    /* 创建输出文件 */
    FILE *img_file = fopen(output_img, "wb+");
    if (!img_file) {
        fprintf(stderr, "Error: Cannot create output file: %s\n", output_img);
        return 1;
    }

    /* 设置文件大小 */
    size_t img_size = (size_t)DEFAULT_TOTAL_BLOCKS * BLOCK_SZ;
    if (fseek(img_file, img_size - 1, SEEK_SET) != 0 || fputc(0, img_file) == EOF) {
        fprintf(stderr, "Error: Cannot set file size\n");
        fclose(img_file);
        return 1;
    }
    fseek(img_file, 0, SEEK_SET);

    /* 初始化块设备 */
    block_file_t dev = { .file = img_file };

    /* 初始化块缓存 */
    block_cache_init();

    /* 创建文件系统 */
    printf("\nCreating file system...\n");
    easy_fs_t *fs = efs_create(&dev, DEFAULT_TOTAL_BLOCKS, DEFAULT_INODE_BITMAP_BLOCKS);
    if (!fs) {
        fprintf(stderr, "Error: Failed to create file system\n");
        fclose(img_file);
        return 1;
    }

    /* 获取根目录 */
    inode_t *root = efs_root_inode(fs);
    if (!root) {
        fprintf(stderr, "Error: Failed to get root inode\n");
        fclose(img_file);
        return 1;
    }

    /* 打包文件 */
    printf("\nPacking files...\n");

    int file_count = 0;

    if (argc > 3) {
        /* 指定了文件列表 */
        for (int i = 3; i < argc; i++) {
            const char *name = argv[i];
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.elf", input_dir, name);

            /* 尝试 .elf 后缀 */
            size_t data_size;
            uint8_t *data = read_file(path, &data_size);

            if (!data) {
                /* 尝试无后缀 */
                snprintf(path, sizeof(path), "%s/%s", input_dir, name);
                data = read_file(path, &data_size);
            }

            if (!data) {
                fprintf(stderr, "Warning: Skipping %s (not found)\n", name);
                continue;
            }

            printf("  %s (%zu bytes)\n", name, data_size);

            inode_t *inode = inode_create(root, name);
            if (!inode) {
                fprintf(stderr, "Error: Failed to create inode for %s\n", name);
                free(data);
                continue;
            }

            inode_write_at(inode, 0, data, data_size);
            free(data);
            free(inode);
            file_count++;
        }
    } else {
        /* 打包目录中所有 .elf 文件 */
        DIR *dir = opendir(input_dir);
        if (!dir) {
            fprintf(stderr, "Error: Cannot open directory: %s\n", input_dir);
            fclose(img_file);
            return 1;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type != DT_REG) continue;

            const char *name = entry->d_name;
            size_t len = strlen(name);

            /* 只打包 .elf 文件 */
            if (len <= 4 || strcmp(name + len - 4, ".elf") != 0) continue;

            char path[512];
            snprintf(path, sizeof(path), "%s/%s", input_dir, name);

            size_t data_size;
            uint8_t *data = read_file(path, &data_size);
            if (!data) continue;

            /* 去掉 .elf 后缀作为文件名 */
            char basename[NAME_LENGTH_LIMIT + 1];
            strncpy(basename, name, len - 4);
            basename[len - 4] = '\0';
            if (strlen(basename) > NAME_LENGTH_LIMIT) {
                basename[NAME_LENGTH_LIMIT] = '\0';
            }

            printf("  %s (%zu bytes)\n", basename, data_size);

            inode_t *inode = inode_create(root, basename);
            if (!inode) {
                fprintf(stderr, "Error: Failed to create inode for %s\n", basename);
                free(data);
                continue;
            }

            inode_write_at(inode, 0, data, data_size);
            free(data);
            free(inode);
            file_count++;
        }

        closedir(dir);
    }

    /* 同步所有缓存 */
    block_cache_sync_all();

    /* 列出文件系统中的文件 */
    printf("\nFiles in fs.img:\n");
    char names[64][NAME_LENGTH_LIMIT + 1];
    size_t count = inode_readdir(root, names, 64);
    for (size_t i = 0; i < count; i++) {
        printf("  %s\n", names[i]);
    }

    printf("\nDone! Packed %d files.\n", file_count);

    free(root);
    free(fs);
    fclose(img_file);

    return 0;
}
