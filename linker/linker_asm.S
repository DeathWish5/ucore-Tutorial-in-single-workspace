/**
 * 链接器辅助汇编函数
 *
 * 使用 lla 指令避免 GOT 访问，确保裸机环境正确工作。
 */

.section .text

/* 获取应用元数据地址 */
.global asm_get_apps
.extern apps
asm_get_apps:
    lla a0, apps
    ret

/* 获取内核符号地址 */
.global asm_get_symbol
.extern __start, __rodata, __data, __sbss, __ebss, __boot, __end

asm_get_symbol:
    /* a0 = index: 0=text, 1=rodata, 2=data, 3=sbss, 4=ebss, 5=boot, 6=end */
    li t0, 0
    beq a0, t0, .L_start
    li t0, 1
    beq a0, t0, .L_rodata
    li t0, 2
    beq a0, t0, .L_data
    li t0, 3
    beq a0, t0, .L_sbss
    li t0, 4
    beq a0, t0, .L_ebss
    li t0, 5
    beq a0, t0, .L_boot
    li t0, 6
    beq a0, t0, .L_end
    li a0, 0
    ret
.L_start:
    lla a0, __start
    ret
.L_rodata:
    lla a0, __rodata
    ret
.L_data:
    lla a0, __data
    ret
.L_sbss:
    lla a0, __sbss
    ret
.L_ebss:
    lla a0, __ebss
    ret
.L_boot:
    lla a0, __boot
    ret
.L_end:
    lla a0, __end
    ret
