/**
 * ch1 - 最小化内核
 *
 * 简单演示：打印 Hello world 然后关机。
 */
#include "../util/sbi.h"

void main(void) {
    const char *msg = "Hello, world!\n";
    while (*msg) {
        console_putchar(*msg++);
    }
    shutdown();
}
