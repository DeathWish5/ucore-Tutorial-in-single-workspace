#include "../user.h"

int main(void) {
    timespec_t start, now;

    sys_clock_gettime(CLOCK_MONOTONIC, &start);

    /* 等待 1 秒 */
    uintptr_t end_sec = start.tv_sec + 1;

    while (1) {
        sys_clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > end_sec ||
            (now.tv_sec == end_sec && now.tv_nsec >= start.tv_nsec)) {
            break;
        }
        sys_sched_yield();
    }

    puts("Test sleep OK!");
    return 0;
}
