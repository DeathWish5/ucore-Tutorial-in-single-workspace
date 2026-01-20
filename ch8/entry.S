.section .text.entry
.global _start

_start:
    lla sp, __end
    j main

.section .boot.stack
.align 12
.space 131072
