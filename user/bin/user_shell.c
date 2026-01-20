/**
 * 用户 Shell
 *
 * 简单的交互式命令行
 */
#include "../user.h"

#define LF  0x0a
#define CR  0x0d
#define BS  0x08
#define DEL 0x7f

#define MAX_LINE 128

static char line[MAX_LINE];
static int line_len = 0;

int main(void) {
    puts("C user shell");
    print_str(">> ");

    while (1) {
        int c = getchar();

        if (c == LF || c == CR) {
            /* 换行 */
            putchar('\n');
            if (line_len > 0) {
                line[line_len] = '\0';

                int pid = sys_fork();
                if (pid == 0) {
                    /* 子进程执行命令 */
                    if (sys_exec(line, line_len) == -1) {
                        puts("Unknown command!");
                        sys_exit(-4);
                    }
                } else {
                    /* 父进程等待子进程 */
                    int exit_code = 0;
                    int exit_pid;
                    /* 循环等待直到子进程退出（waitpid 返回 -2 表示子进程还在运行） */
                    while ((exit_pid = sys_waitpid(pid, &exit_code)) == -2) {
                        sys_sched_yield();
                    }
                    print_str("Shell: Process ");
                    print_int(exit_pid);
                    print_str(" exited with code ");
                    print_int(exit_code);
                    putchar('\n');
                }

                line_len = 0;
            }
            print_str(">> ");
        } else if (c == BS || c == DEL) {
            /* 退格 */
            if (line_len > 0) {
                putchar(BS);
                putchar(' ');
                putchar(BS);
                line_len--;
            }
        } else if (line_len < MAX_LINE - 1) {
            /* 普通字符 */
            putchar(c);
            line[line_len++] = c;
        }
    }

    return 0;
}
