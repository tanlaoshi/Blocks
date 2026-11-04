/*
 * stdio_fmt.c — fprintf / fputs / fputc / putchar（全称下划线）
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <Blocks/Syscall.h>

int putchar(int character) {
    unsigned char byte = (unsigned char)character;

    if (fputc((int)byte, stdout) == EOF) {
        return EOF;
    }
    return (int)byte;
}

int fputc(int character, FILE *stream) {
    unsigned char byte = (unsigned char)character;

    if (!stream) {
        return EOF;
    }
    if (fwrite(&byte, 1, 1, stream) != 1) {
        return EOF;
    }
    return (int)byte;
}

int fputs(const char *string, FILE *stream) {
    size_t length;

    if (!stream || !string) {
        return EOF;
    }
    length = strlen(string);
    if (length > 0 && fwrite(string, 1, length, stream) != length) {
        return EOF;
    }
    return 0;
}

int vfprintf(FILE *stream, const char *format, va_list arguments) {
    char buffer[512];
    int printed;
    size_t write_count;

    if (!stream || !format) {
        return -1;
    }
    printed = vsnprintf(buffer, sizeof(buffer), format, arguments);
    if (printed < 0) {
        return printed;
    }
    write_count = (size_t)printed;
    if (write_count >= sizeof(buffer)) {
        write_count = sizeof(buffer) - 1u;
    }
    if (write_count > 0 && fwrite(buffer, 1, write_count, stream) != write_count) {
        return -1;
    }
    return printed;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list arguments;
    int printed;

    va_start(arguments, format);
    printed = vfprintf(stream, format, arguments);
    va_end(arguments);
    return printed;
}
