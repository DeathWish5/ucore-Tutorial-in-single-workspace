/**
 * 内核入口
 */
.section .text.entry
.global _start

_start:
    lla sp, stack
    li t0, 4096
    add sp, sp, t0
    j main

/* 栈空间 (4KB) */
.section .bss.uninit
.global stack
stack:
    .space 4096
