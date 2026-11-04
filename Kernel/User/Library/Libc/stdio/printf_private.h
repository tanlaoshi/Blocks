/*
 * printf_private.h — FmtOut（仅 crt/printf*.c）
 */
#ifndef PRINTF_PRIVATE_H
#define PRINTF_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

typedef struct {
    char *buffer;
    size_t capacity;
    size_t position;
    int write_to_descriptor; /* 1：逐字写 fd 1（puts 等已改缓冲路径后少用） */
} fmt_out;

void printf_out_char(fmt_out *output, char character);
void printf_out_str(fmt_out *output, const char *string);
int printf_format(fmt_out *output, const char *format, va_list arguments);

#endif
