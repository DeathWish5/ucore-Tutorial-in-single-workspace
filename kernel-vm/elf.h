/**
 * 简单 ELF 加载器
 *
 * 只支持加载 ELF64 可执行文件到地址空间
 */
#ifndef ELF_H
#define ELF_H

#include "address_space.h"
#include <stdint.h>

/* ELF64 文件头 */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

/* ELF64 程序头 */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

/* ELF 常量 */
#define EI_MAG0     0
#define ELFMAG0     0x7f
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'

#define ET_EXEC     2           /* Executable file */

#define EM_RISCV    243         /* RISC-V */

#define PT_LOAD     1           /* Loadable segment */

#define PF_X        0x1         /* Executable */
#define PF_W        0x2         /* Writable */
#define PF_R        0x4         /* Readable */

/**
 * 验证 ELF 文件头
 *
 * @param data ELF 文件数据
 * @param len  数据长度
 * @return 成功返回入口地址，失败返回 0
 */
uintptr_t elf_check(const uint8_t *data, size_t len);

/**
 * 加载 ELF 到地址空间
 *
 * @param as   目标地址空间
 * @param data ELF 文件数据
 * @param len  数据长度
 * @return 成功返回入口地址，失败返回 0
 */
uintptr_t elf_load(address_space_t *as, const uint8_t *data, size_t len);

#endif /* ELF_H */
