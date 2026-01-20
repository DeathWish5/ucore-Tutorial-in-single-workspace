/**
 * 内核入口
 */
.section .text.entry
.global _start

_start:
    lla sp, __end       /* 栈指针设为内核末尾 */
    j main

/* 启动栈 */
.section .boot.stack
.align 12
.space 16384            /* 16KB */
