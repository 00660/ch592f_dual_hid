#include "diag.h"
#include <stdarg.h>

static int emit(char value)
{
    if (value == '\n') diag_putc('\r');
    diag_putc(value);
    return 1;
}

int diag_printf(const char *format, ...)
{
    va_list args;
    int length = 0;
    if (diag_is_quiet()) return 0;
    va_start(args,format);
    while (*format) {
        unsigned width = 0, base, count;
        uint32_t value;
        char digits[10], pad = ' ';
        if (*format != '%') {
            length += emit(*format++);
            continue;
        }
        ++format;
        if (*format == '0') { pad = '0'; ++format; }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (unsigned)(*format++ - '0');
            if (width > 10) width = 10;
        }
        if (!*format) break;
        if (*format == 's') {
            const char *text = va_arg(args,const char *);
            if (!text) text = "(null)";
            while (*text) length += emit(*text++);
        } else if (*format == 'c') {
            length += emit((char)va_arg(args,int));
        } else if (*format == 'x' || *format == 'X' || *format == 'u' || *format == 'd') {
            base = *format == 'x' || *format == 'X' ? 16 : 10;
            if (*format == 'd') {
                int signed_value = va_arg(args,int);
                value = (uint32_t)signed_value;
                if (signed_value < 0) {
                    length += emit('-');
                    value = 0u - value;
                    if (width) --width;
                }
            } else value = va_arg(args,unsigned);
            count = 0;
            do {
                digits[count++] = "0123456789ABCDEF"[value % base];
                value /= base;
            } while (value);
            while (width > count) { length += emit(pad); --width; }
            while (count) length += emit(digits[--count]);
        } else length += emit(*format);
        ++format;
    }
    va_end(args);
    return length;
}
