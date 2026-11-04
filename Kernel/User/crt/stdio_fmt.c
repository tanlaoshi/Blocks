/*
 * stdio_fmt.c — fprintf / fputs / fputc / putchar（libc-stdio-fmt）
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <Blocks/syscall.h>

int putchar(int c) {
    unsigned char Byte = (unsigned char)c;

    /* 经 stdout 缓冲，避免逐字 syscall */
    if (fputc((int)Byte, stdout) == EOF) {
        return EOF;
    }
    return (int)Byte;
}

int fputc(int c, FILE *fp) {
    unsigned char Ch = (unsigned char)c;

    if (!fp) {
        return EOF;
    }
    if (fwrite(&Ch, 1, 1, fp) != 1) {
        return EOF;
    }
    return (int)Ch;
}

int fputs(const char *s, FILE *fp) {
    size_t N;

    if (!fp || !s) {
        return EOF;
    }
    N = strlen(s);
    if (N > 0 && fwrite(s, 1, N, fp) != N) {
        return EOF;
    }
    return 0;
}

int vfprintf(FILE *fp, const char *fmt, va_list ap) {
    char Buf[512];
    int N;
    size_t W;

    if (!fp || !fmt) {
        return -1;
    }
    N = vsnprintf(Buf, sizeof(Buf), fmt, ap);
    if (N < 0) {
        return N;
    }
    W = (size_t)N;
    if (W >= sizeof(Buf)) {
        W = sizeof(Buf) - 1u;
    }
    if (W > 0 && fwrite(Buf, 1, W, fp) != W) {
        return -1;
    }
    return N;
}

int fprintf(FILE *fp, const char *fmt, ...) {
    va_list Ap;
    int N;

    va_start(Ap, fmt);
    N = vfprintf(fp, fmt, Ap);
    va_end(Ap);
    return N;
}
