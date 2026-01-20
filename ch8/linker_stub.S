.section .data
.global apps
apps:
    .quad 0
    .quad 0
    .quad 0

.section .text

.global asm_get_apps
asm_get_apps:
    lla a0, apps
    ret

.global asm_get_symbol
.extern __start, __rodata, __data, __sbss, __ebss, __boot, __end

asm_get_symbol:
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
