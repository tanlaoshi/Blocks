/*
 * printf.c — printf / sprintf / snprintf / puts / perror（全称下划线）
 *
 * 写 stdout 一律整段 write，避免逐字 SYS_FILE_WRITE→PresentFlush。
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <Blocks/syscall.h>
#include "printf_private.h"

int vprintf(const char *format, va_list arguments) {
    char buffer[512];
    int printed;
    size_t write_count;

    printed = vsnprintf(buffer, sizeof(buffer), format, arguments);
    if (printed < 0) {
        return printed;
    }
    write_count = (size_t)printed;
    if (write_count >= sizeof(buffer)) {
        write_count = sizeof(buffer) - 1u;
    }
    if (write_count > 0) {
        blocks_write(1, buffer, write_count);
    }
    return printed;
}

int printf(const char *format, ...) {
    va_list arguments;
    int printed;

    va_start(arguments, format);
    printed = vprintf(format, arguments);
    va_end(arguments);
    return printed;
}

int vsnprintf(char *buffer, size_t capacity, const char *format,
              va_list arguments) {
    fmt_out output;
    int printed;

    output.buffer = buffer;
    output.capacity = capacity;
    output.position = 0;
    output.write_to_descriptor = 0;
    printed = printf_format(&output, format, arguments);
    if (buffer && capacity > 0) {
        size_t end =
            (output.position < capacity) ? output.position : capacity - 1;

        buffer[end] = 0;
    }
    return printed;
}

int snprintf(char *buffer, size_t capacity, const char *format, ...) {
    va_list arguments;
    int printed;

    va_start(arguments, format);
    printed = vsnprintf(buffer, capacity, format, arguments);
    va_end(arguments);
    return printed;
}

int vsprintf(char *buffer, const char *format, va_list arguments) {
    fmt_out output;
    int printed;

    output.buffer = buffer;
    output.capacity = (size_t)-1;
    output.position = 0;
    output.write_to_descriptor = 0;
    printed = printf_format(&output, format, arguments);
    if (buffer) {
        buffer[output.position] = 0;
    }
    return printed;
}

int sprintf(char *buffer, const char *format, ...) {
    va_list arguments;
    int printed;

    va_start(arguments, format);
    printed = vsprintf(buffer, format, arguments);
    va_end(arguments);
    return printed;
}

int puts(const char *string) {
    char buffer[512];
    int printed;

    printed = snprintf(buffer, sizeof(buffer), "%s\n", string ? string : "(null)");
    if (printed < 0) {
        return printed;
    }
    if (printed > 0) {
        size_t write_count = (size_t)printed;

        if (write_count >= sizeof(buffer)) {
            write_count = sizeof(buffer) - 1u;
        }
        blocks_write(1, buffer, write_count);
    }
    return printed;
}

void perror(const char *prefix) {
    char buffer[512];

    if (prefix && prefix[0] != 0) {
        snprintf(buffer, sizeof(buffer), "%s: %s\n", prefix, strerror(errno));
    } else {
        snprintf(buffer, sizeof(buffer), "%s\n", strerror(errno));
    }
    blocks_write(1, buffer, strlen(buffer));
}
