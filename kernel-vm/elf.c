/**
 * 简单 ELF 加载器实现
 */
#include "elf.h"
#include <string.h>

uintptr_t elf_check(const uint8_t *data, size_t len) {
    if (len < sizeof(elf64_ehdr_t)) {
        return 0;
    }

    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)data;

    /* 检查 magic number */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3) {
        return 0;
    }

    /* 检查是否是 64 位 */
    if (ehdr->e_ident[4] != 2) {  /* ELFCLASS64 */
        return 0;
    }

    /* 检查是否是可执行文件 */
    if (ehdr->e_type != ET_EXEC) {
        return 0;
    }

    /* 检查是否是 RISC-V */
    if (ehdr->e_machine != EM_RISCV) {
        return 0;
    }

    return ehdr->e_entry;
}

uintptr_t elf_load(address_space_t *as, const uint8_t *data, size_t len) {
    uintptr_t entry = elf_check(data, len);
    if (!entry) {
        return 0;
    }

    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)data;
    const elf64_phdr_t *phdrs = (const elf64_phdr_t *)(data + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t *phdr = &phdrs[i];

        /* 只加载 PT_LOAD 段 */
        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        uint64_t off_file = phdr->p_offset;
        uint64_t len_file = phdr->p_filesz;
        uint64_t off_mem = phdr->p_vaddr;
        uint64_t end_mem = off_mem + phdr->p_memsz;

        /* 计算 VPN 范围 */
        uintptr_t vpn_start = va_vpn(off_mem) & ~0UL;  /* floor */
        uintptr_t vpn_end = (va_vpn(end_mem - 1)) + 1; /* ceil */

        /* 构建标志 */
        uint64_t flags = PTE_V | PTE_U;
        if (phdr->p_flags & PF_R) flags |= PTE_R;
        if (phdr->p_flags & PF_W) flags |= PTE_W;
        if (phdr->p_flags & PF_X) flags |= PTE_X;

        /* 映射并拷贝数据 */
        size_t offset = off_mem & PAGE_MASK;
        as_map(as, vpn_start, vpn_end,
               data + off_file, len_file, offset, flags);
    }

    return entry;
}
