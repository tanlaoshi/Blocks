/*
 * printf.c — printf / sprintf / snprintf / puts / perror（展览级命名）
 *
 * 写 stdout 一律整段 write，避免逐字 SYS_FILE_WRITE→PresentFlush。
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <Blocks/syscall.h>
#include "printf_private.h"

int vprintf(const char *Format, va_list Arguments) {
    char Buffer[512];
    int Printed;
    size_t WriteCount;

    Printed = vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    if (Printed < 0) {
        return Printed;
    }
    WriteCount = (size_t)Printed;
    if (WriteCount >= sizeof(Buffer)) {
        WriteCount = sizeof(Buffer) - 1u;
    }
    if (WriteCount > 0) {
        blocks_write(1, Buffer, WriteCount);
    }
    return Printed;
}

int printf(const char *Format, ...) {
    va_list Arguments;
    int Printed;

    va_start(Arguments, Format);
    Printed = vprintf(Format, Arguments);
    va_end(Arguments);
    return Printed;
}

int vsnprintf(char *Buffer, size_t Capacity, const char *Format,
              va_list Arguments) {
    FmtOut Output;
    int Printed;

    Output.Buffer = Buffer;
    Output.Capacity = Capacity;
    Output.Position = 0;
    Output.WriteToDescriptor = 0;
    Printed = PrintfFormat(&Output, Format, Arguments);
    if (Buffer && Capacity > 0) {
        size_t End =
            (Output.Position < Capacity) ? Output.Position : Capacity - 1;

        Buffer[End] = 0;
    }
    return Printed;
}

int snprintf(char *Buffer, size_t Capacity, const char *Format, ...) {
    va_list Arguments;
    int Printed;

    va_start(Arguments, Format);
    Printed = vsnprintf(Buffer, Capacity, Format, Arguments);
    va_end(Arguments);
    return Printed;
}

int vsprintf(char *Buffer, const char *Format, va_list Arguments) {
    FmtOut Output;
    int Printed;

    Output.Buffer = Buffer;
    Output.Capacity = (size_t)-1;
    Output.Position = 0;
    Output.WriteToDescriptor = 0;
    Printed = PrintfFormat(&Output, Format, Arguments);
    if (Buffer) {
        Buffer[Output.Position] = 0;
    }
    return Printed;
}

int sprintf(char *Buffer, const char *Format, ...) {
    va_list Arguments;
    int Printed;

    va_start(Arguments, Format);
    Printed = vsprintf(Buffer, Format, Arguments);
    va_end(Arguments);
    return Printed;
}

int puts(const char *String) {
    char Buffer[512];
    int Printed;

    Printed = snprintf(Buffer, sizeof(Buffer), "%s\n", String ? String : "(null)");
    if (Printed < 0) {
        return Printed;
    }
    if (Printed > 0) {
        size_t WriteCount = (size_t)Printed;

        if (WriteCount >= sizeof(Buffer)) {
            WriteCount = sizeof(Buffer) - 1u;
        }
        blocks_write(1, Buffer, WriteCount);
    }
    return Printed;
}

void perror(const char *Prefix) {
    char Buffer[512];

    if (Prefix && Prefix[0] != 0) {
        snprintf(Buffer, sizeof(Buffer), "%s: %s\n", Prefix, strerror(errno));
    } else {
        snprintf(Buffer, sizeof(Buffer), "%s\n", strerror(errno));
    }
    blocks_write(1, Buffer, strlen(Buffer));
}
