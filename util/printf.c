/**
 * 简易 printf 实现
 */
#include "printf.h"
#include "sbi.h"
#include <stdarg.h>
#include <stdint.h>

static const char HEX_DIGITS[] = "0123456789abcdef";

static void print_int(int value, int base, int is_signed) {
    char buf[20];
    int i = 0;
    unsigned int uval;

    if (is_signed && value < 0) {
        uval = -value;
    } else {
        uval = value;
    }

    do {
        buf[i++] = HEX_DIGITS[uval % base];
        uval /= base;
    } while (uval);

    if (is_signed && value < 0) {
        buf[i++] = '-';
    }

    while (--i >= 0) {
        console_putchar(buf[i]);
    }
}

static void print_ptr(uint64_t ptr) {
    console_putchar('0');
    console_putchar('x');
    for (int i = 60; i >= 0; i -= 4) {
        console_putchar(HEX_DIGITS[(ptr >> i) & 0xf]);
    }
}

void puts(const char *s) {
    if (!s) return;
    while (*s) {
        console_putchar(*s++);
    }
    console_putchar('\n');
}

void printf(const char *fmt, ...) {
    if (!fmt) return;

    va_list ap;
    va_start(ap, fmt);

    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            console_putchar(fmt[i]);
            continue;
        }

        switch (fmt[++i]) {
        case 'd':
            print_int(va_arg(ap, int), 10, 1);
            break;
        case 'x':
            print_int(va_arg(ap, int), 16, 0);
            break;
        case 'p':
            print_ptr(va_arg(ap, uint64_t));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            while (s && *s) console_putchar(*s++);
            break;
        }
        case '%':
            console_putchar('%');
            break;
        case '\0':
            goto done;
        default:
            console_putchar('%');
            console_putchar(fmt[i]);
            break;
        }
    }
done:
    va_end(ap);
}
