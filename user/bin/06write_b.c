#include "../user.h"

#define WIDTH   10
#define HEIGHT  2

int main(void) {
    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            putchar('B');
        }
        print_str(" [");
        print_int(i + 1);
        putchar('/');
        print_int(HEIGHT);
        puts("]");
        sys_sched_yield();
    }
    puts("Test write_b OK!");
    return 0;
}
