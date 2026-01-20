/**
 * 简单文件测试
 */
#include "../user.h"

int main(void) {
    const char *test_str = "Hello, world!";
    const char *filename = "filea";

    /* 创建并写入文件 */
    int fd = sys_open(filename, O_CREATE | O_WRONLY);
    if (fd < 0) {
        puts("open for write failed!");
        sys_exit(-1);
    }

    sys_write(fd, test_str, 13);
    sys_close(fd);

    /* 读取文件 */
    fd = sys_open(filename, O_RDONLY);
    if (fd < 0) {
        puts("open for read failed!");
        sys_exit(-1);
    }

    char buf[100];
    int n = sys_read(fd, buf, 100);
    sys_close(fd);

    /* 验证 */
    if (n == 13) {
        buf[n] = '\0';
        print_str("read: ");
        puts(buf);
        puts("file_test passed!");
    } else {
        puts("file_test failed!");
        sys_exit(-1);
    }

    return 0;
}
