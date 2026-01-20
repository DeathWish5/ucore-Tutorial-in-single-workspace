/**
 * fork 测试
 */
#include "../user.h"

#define MAX_CHILD 10

int main(void) {
    for (int i = 0; i < MAX_CHILD; i++) {
        int pid = sys_fork();
        if (pid == 0) {
            /* 子进程 */
            print_str("I am child ");
            print_int(i);
            putchar('\n');
            sys_exit(0);
        } else {
            print_str("forked child pid = ");
            print_int(pid);
            putchar('\n');
        }
    }

    int exit_code = 0;
    for (int i = 0; i < MAX_CHILD; i++) {
        int pid = wait(&exit_code);
        if (pid <= 0) {
            puts("wait stopped early");
            sys_exit(-1);
        }
    }

    if (wait(&exit_code) > 0) {
        puts("wait got too many");
        sys_exit(-1);
    }

    puts("forktest pass.");
    return 0;
}
