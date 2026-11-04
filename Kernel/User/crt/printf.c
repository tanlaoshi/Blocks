/*
 * printf.c — printf / sprintf / snprintf / puts / perror
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

    /*
     * 整段一次 write：避免逐字 SYS_FILE_WRITE→Shell PresentFlush（关中断下会卡死）。
     */
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
    va_list Ap;
    int N;

    va_start(Ap, fmt);
    N = vprintf(fmt, Ap);
    va_end(Ap);
    return N;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    FmtOut O;
    int N;

    O.Buf = buf;
    O.Cap = size;
    O.Pos = 0;
    O.ToFd = 0;
    N = PrintfFormat(&O, fmt, ap);
    if (buf) {
        if (size == 0) {
            buf[O.Pos] = 0;
        } else {
            size_t End = (O.Pos < size) ? O.Pos : size - 1;
            buf[End] = 0;
        }
    }
    return N;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list Ap;
    int N;

    va_start(Ap, fmt);
    N = vsnprintf(buf, size, fmt, Ap);
    va_end(Ap);
    return N;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, 0, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list Ap;
    int N;

    va_start(Ap, fmt);
    N = vsprintf(buf, fmt, Ap);
    va_end(Ap);
    return N;
}

int puts(const char *s) {
    FmtOut O;

    O.Buf = 0;
    O.Cap = 0;
    O.Pos = 0;
    O.ToFd = 1;
    PrintfOutStr(&O, s);
    PrintfOutChar(&O, '\n');
    return (int)O.Pos;
}

void perror(const char *s) {
    FmtOut O;

    O.Buf = 0;
    O.Cap = 0;
    O.Pos = 0;
    O.ToFd = 1;
    if (s && s[0]) {
        PrintfOutStr(&O, s);
        PrintfOutStr(&O, ": ");
    }
    PrintfOutStr(&O, strerror(errno));
    PrintfOutChar(&O, '\n');
}
