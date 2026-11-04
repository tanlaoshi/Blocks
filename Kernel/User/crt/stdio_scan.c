/*
 * stdio_scan.c — fgetc / fgets / getchar / sscanf / scanf / fscanf
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "stdio_private.h"

int BlocksVsscanf(const char *String, const char *Format, va_list Arguments);

int fgetc(FILE *fp) {
    unsigned char Ch;
    size_t N;

    if (!fp) {
        return EOF;
    }
    N = fread(&Ch, 1, 1, fp);
    if (N != 1) {
        return EOF;
    }
    return (int)Ch;
}

int getchar(void) {
    return fgetc(stdin);
}

char *fgets(char *s, int size, FILE *fp) {
    int I;
    int C;

    if (!s || size <= 0 || !fp) {
        return 0;
    }
    for (I = 0; I < size - 1; I++) {
        C = fgetc(fp);
        if (C == EOF) {
            if (I == 0) {
                return 0;
            }
            break;
        }
        s[I] = (char)C;
        if (C == '\n') {
            I++;
            break;
        }
    }
    s[I] = 0;
    return s;
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list Arguments;
    int Count;

    if (!str || !fmt) {
        return EOF;
    }
    va_start(Arguments, fmt);
    Count = BlocksVsscanf(str, fmt, Arguments);
    va_end(Arguments);
    return Count;
}

int scanf(const char *fmt, ...) {
    char Line[256];
    va_list Arguments;
    int Count;

    if (!fmt) {
        return EOF;
    }
    if (!fgets(Line, (int)sizeof(Line), stdin)) {
        return EOF;
    }
    va_start(Arguments, fmt);
    Count = BlocksVsscanf(Line, fmt, Arguments);
    va_end(Arguments);
    return Count;
}

int fscanf(FILE *fp, const char *fmt, ...) {
    char Line[256];
    va_list Arguments;
    int Count;

    if (!fp || !fmt) {
        return EOF;
    }
    if (!fgets(Line, (int)sizeof(Line), fp)) {
        return EOF;
    }
    va_start(Arguments, fmt);
    Count = BlocksVsscanf(Line, fmt, Arguments);
    va_end(Arguments);
    return Count;
}
