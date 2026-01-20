#include "../user.h"

#define WIDTH   10
#define HEIGHT  5

int main(void) {
    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            putchar('A');
        }
        print_str(" [");
        print_int(i + 1);
        putchar('/');
        print_int(HEIGHT);
        puts("]");
        sys_sched_yield();
    }
    puts("Test write_a OK!");
    return 0;
}
