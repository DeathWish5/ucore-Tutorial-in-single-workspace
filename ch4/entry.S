/**
 * 内核入口
 */
.section .text.entry
.global _start

_start:
    lla sp, __end
    j main

/* 启动栈 (64KB) */
.section .boot.stack
.align 12
.space 65536
