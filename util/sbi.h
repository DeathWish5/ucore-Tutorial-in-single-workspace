/**
 * SBI (Supervisor Binary Interface) 调用接口
 */
#ifndef SBI_H
#define SBI_H

#include <stdint.h>

/* SBI 扩展号 */
#define SBI_EXT_LEGACY_SET_TIMER        0x00
#define SBI_EXT_LEGACY_CONSOLE_PUTCHAR  0x01
#define SBI_EXT_LEGACY_CONSOLE_GETCHAR  0x02
#define SBI_EXT_SRST                    0x53525354

/* 系统复位类型和原因 */
#define SBI_RESET_TYPE_SHUTDOWN     0
#define SBI_RESET_REASON_NONE       0
#define SBI_RESET_REASON_FAILURE    1

/* 通用 SBI ecall */
long sbi_call(int ext, int fid, unsigned long arg0, unsigned long arg1,
              unsigned long arg2, unsigned long arg3,
              unsigned long arg4, unsigned long arg5);

/* 输出单个字符到控制台 */
void console_putchar(int ch);

/* 从控制台读取单个字符 */
int console_getchar(void);

/* 设置定时器 (触发时钟中断) */
void sbi_set_timer(uint64_t time);

/* 系统复位/关机 */
void shutdown(void);

#endif /* SBI_H */
