/*
 * printf_private.h — FmtOut（仅 crt/printf*.c）
 */
#ifndef PRINTF_PRIVATE_H
#define PRINTF_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

typedef struct {
    char *Buffer;
    size_t Capacity;
    size_t Position;
    int WriteToDescriptor; /* 1：逐字写 fd 1（puts 等已改缓冲路径后少用） */
} FmtOut;

void PrintfOutChar(FmtOut *Output, char Character);
void PrintfOutStr(FmtOut *Output, const char *String);
int PrintfFormat(FmtOut *Output, const char *Format, va_list Arguments);

#endif
