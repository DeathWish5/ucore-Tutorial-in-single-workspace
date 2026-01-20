/**
 * 读取并显示 filea 的内容
 */
#include "../user.h"

int main(void) {
    int fd = sys_open("filea", O_RDONLY);
    if (fd < 0) {
        puts("Error opening filea");
        sys_exit(-1);
    }

    char buf[256];
    while (1) {
        int n = sys_read(fd, buf, 256);
        if (n <= 0) break;
        sys_write(STDOUT, buf, n);
    }
    putchar('\n');

    sys_close(fd);
    return 0;
}
