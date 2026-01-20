/**
 * 简易 printf 实现
 */
#ifndef PRINTF_H
#define PRINTF_H

/* 输出字符串（带换行） */
void puts(const char *s);

/* 格式化输出，支持: %d %x %p %s %% */
void printf(const char *fmt, ...);

#endif /* PRINTF_H */
