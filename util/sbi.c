/**
 * SBI 调用实现
 */
#include "sbi.h"

long sbi_call(int ext, int fid, unsigned long arg0, unsigned long arg1,
              unsigned long arg2, unsigned long arg3,
              unsigned long arg4, unsigned long arg5) {
    register unsigned long a0 asm("a0") = arg0;
    register unsigned long a1 asm("a1") = arg1;
    register unsigned long a2 asm("a2") = arg2;
    register unsigned long a3 asm("a3") = arg3;
    register unsigned long a4 asm("a4") = arg4;
    register unsigned long a5 asm("a5") = arg5;
    register unsigned long a6 asm("a6") = fid;
    register unsigned long a7 asm("a7") = ext;

    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    return a0;
}

void console_putchar(int ch) {
    sbi_call(SBI_EXT_LEGACY_CONSOLE_PUTCHAR, 0, ch, 0, 0, 0, 0, 0);
}

int console_getchar(void) {
    return sbi_call(SBI_EXT_LEGACY_CONSOLE_GETCHAR, 0, 0, 0, 0, 0, 0, 0);
}

void sbi_set_timer(uint64_t time) {
    sbi_call(SBI_EXT_LEGACY_SET_TIMER, 0, time, 0, 0, 0, 0, 0);
}

void shutdown(void) {
    sbi_call(SBI_EXT_SRST, 0, SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE, 0, 0, 0, 0);
    while (1) {}
}
