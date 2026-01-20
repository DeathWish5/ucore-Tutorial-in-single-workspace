/**
 * 初始进程
 *
 * fork 并 exec user_shell，然后回收僵尸进程
 */
#include "../user.h"

int main(void) {
    int pid = sys_fork();
    if (pid == 0) {
        /* 子进程：执行 shell */
        sys_exec("user_shell", 10);
        /* exec 失败 */
        puts("exec user_shell failed!");
        sys_exit(-1);
    } else {
        /* 父进程：循环回收僵尸进程 */
        while (1) {
            int exit_code = 0;
            int dead_pid = wait(&exit_code);
            if (dead_pid == -1) {
                sys_sched_yield();
                continue;
            }
            /* 可以打印回收的进程信息 */
        }
    }
    return 0;
}
