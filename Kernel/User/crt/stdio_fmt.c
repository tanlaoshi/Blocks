/*
 * stdio_fmt.c — fprintf / fputs / fputc / putchar（展览级命名）
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <Blocks/syscall.h>

int putchar(int Character) {
    unsigned char Byte = (unsigned char)Character;

    if (fputc((int)Byte, stdout) == EOF) {
        return EOF;
    }
    return (int)Byte;
}

int fputc(int Character, FILE *Stream) {
    unsigned char Byte = (unsigned char)Character;

    if (!Stream) {
        return EOF;
    }
    if (fwrite(&Byte, 1, 1, Stream) != 1) {
        return EOF;
    }
    return (int)Byte;
}

int fputs(const char *String, FILE *Stream) {
    size_t Length;

    if (!Stream || !String) {
        return EOF;
    }
    Length = strlen(String);
    if (Length > 0 && fwrite(String, 1, Length, Stream) != Length) {
        return EOF;
    }
    return 0;
}

int vfprintf(FILE *Stream, const char *Format, va_list Arguments) {
    char Buffer[512];
    int Printed;
    size_t WriteCount;

    if (!Stream || !Format) {
        return -1;
    }
    Printed = vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    if (Printed < 0) {
        return Printed;
    }
    WriteCount = (size_t)Printed;
    if (WriteCount >= sizeof(Buffer)) {
        WriteCount = sizeof(Buffer) - 1u;
    }
    if (WriteCount > 0 && fwrite(Buffer, 1, WriteCount, Stream) != WriteCount) {
        return -1;
    }
    return Printed;
}

int fprintf(FILE *Stream, const char *Format, ...) {
    va_list Arguments;
    int Printed;

    va_start(Arguments, Format);
    Printed = vfprintf(Stream, Format, Arguments);
    va_end(Arguments);
    return Printed;
}
