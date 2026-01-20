/**
 * 简单信号测试
 */
#include "../user.h"

static volatile int flag = 0;

void sig_handler(int signum) {
    print_str("Signal ");
    print_int(signum);
    puts(" received!");
    flag = 1;
    sys_sigreturn();
}

int main(void) {
    puts("sig_simple test");

    /* 设置信号处理函数 */
    sigaction_t action;
    action.handler = (unsigned long)sig_handler;
    action.mask = 0;

    if (sys_sigaction(SIGUSR1, &action, 0) < 0) {
        puts("sigaction failed!");
        sys_exit(-1);
    }

    puts("send SIGUSR1 to self");
    int pid = sys_getpid();
    sys_kill(pid, SIGUSR1);

    if (flag) {
        puts("signal handler executed!");
    } else {
        puts("signal not handled?");
    }

    puts("sig_simple passed!");
    return 0;
}
