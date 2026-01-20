/**
 * ch5 链接脚本
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS {
    . = 0x80200000;

    .text : {
        __start = .;
        *(.text.entry)
        *(.text .text.*)
    }

    .rodata : ALIGN(4096) {
        __rodata = .;
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4096) {
        __data = .;
        *(.data .data.*)
    }

    .bss : ALIGN(4096) {
        __sbss = .;
        *(.bss .bss.*)
        __ebss = .;
    }

    .boot : ALIGN(4096) {
        __boot = .;
        KEEP(*(.boot.stack))
    }

    __end = .;
}
