/*
 * printf_private.h — FmtOut（仅 crt/printf*.c）
 */
#ifndef PRINTF_PRIVATE_H
#define PRINTF_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

typedef struct {
    char *Buf;
    size_t Cap;
    size_t Pos;
    int ToFd;
} FmtOut;

void PrintfOutChar(FmtOut *O, char C);
void PrintfOutStr(FmtOut *O, const char *S);
int PrintfFormat(FmtOut *O, const char *fmt, va_list ap);

#endif
