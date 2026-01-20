/**
 * 用户程序入口
 */
.section .text.entry
.global _start

_start:
    call main
    mv a0, a0       /* main 返回值作为 exit 参数 */
    call sys_exit
    j .             /* 不应到达 */
