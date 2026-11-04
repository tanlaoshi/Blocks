/*
 * printf.c — printf / sprintf / snprintf / puts / perror
 *
 * 写 stdout 一律整段 write，避免逐字 SYS_FILE_WRITE→PresentFlush。
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <Blocks/syscall.h>
#include "printf_private.h"

int vprintf(const char *fmt, va_list ap) {
    char Buf[512];
    int N;
    size_t W;

    N = vsnprintf(Buf, sizeof(Buf), fmt, ap);
    if (N < 0) {
        return N;
    }
    W = (size_t)N;
    if (W >= sizeof(Buf)) {
        W = sizeof(Buf) - 1u;
    }
    if (W > 0) {
        blocks_write(1, Buf, W);
    }
    return N;
}

int printf(const char *fmt, ...) {
    va_list Arguments;
    int N;

    va_start(Arguments, fmt);
    N = vprintf(fmt, Arguments);
    va_end(Arguments);
    return N;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    FmtOut Output;
    int N;

    Output.Buf = buf;
    Output.Cap = size;
    Output.Pos = 0;
    Output.ToFd = 0;
    N = PrintfFormat(&Output, fmt, ap);
    if (buf && size > 0) {
        size_t End = (Output.Pos < size) ? Output.Pos : size - 1;

        buf[End] = 0;
    }
    return N;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list Arguments;
    int N;

    va_start(Arguments, fmt);
    N = vsnprintf(buf, size, fmt, Arguments);
    va_end(Arguments);
    return N;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    FmtOut Output;
    int N;

    /* 无上界：课用 sprintf；Cap 极大使 Pos+1 < Cap 在实用长度内恒真 */
    Output.Buf = buf;
    Output.Cap = (size_t)-1;
    Output.Pos = 0;
    Output.ToFd = 0;
    N = PrintfFormat(&Output, fmt, ap);
    if (buf) {
        buf[Output.Pos] = 0;
    }
    return N;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list Arguments;
    int N;

    va_start(Arguments, fmt);
    N = vsprintf(buf, fmt, Arguments);
    va_end(Arguments);
    return N;
}

int puts(const char *s) {
    char Buf[512];
    int N;

    N = snprintf(Buf, sizeof(Buf), "%s\n", s ? s : "(null)");
    if (N < 0) {
        return N;
    }
    if (N > 0) {
        size_t W = (size_t)N;

        if (W >= sizeof(Buf)) {
            W = sizeof(Buf) - 1u;
        }
        blocks_write(1, Buf, W);
    }
    return N;
}

void perror(const char *s) {
    char Buf[512];

    if (s && s[0]) {
        snprintf(Buf, sizeof(Buf), "%s: %s\n", s, strerror(errno));
    } else {
        snprintf(Buf, sizeof(Buf), "%s\n", strerror(errno));
    }
    blocks_write(1, Buf, strlen(Buf));
}
