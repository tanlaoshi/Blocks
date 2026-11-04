/*
 * printf_format.c — PrintfFormat：整数 / 长度修饰 / 浮点挂接（展览级铺平）
 *
 * LengthKind：0 默认 / 1 = l / 2 = ll / 3 = z
 */
#include <stdarg.h>
#include <stdint.h>
#include <Blocks/syscall.h>
#include "printf_private.h"

size_t PrintfFormatDouble(char *Destination, size_t Capacity, double Value,
                          int Specifier, int Precision);

enum {
    LengthDefault = 0,
    LengthLong = 1,
    LengthLongLong = 2,
    LengthSize = 3
};

void PrintfOutChar(FmtOut *Output, char Character) {
    if (Output->WriteToDescriptor) {
        blocks_write(1, &Character, 1);
        Output->Position++;
        return;
    }
    if (Output->Buffer && Output->Capacity > 0 &&
        Output->Position + 1 < Output->Capacity) {
        Output->Buffer[Output->Position] = Character;
    }
    Output->Position++;
}

void PrintfOutStr(FmtOut *Output, const char *String) {
    if (!String) {
        String = "(null)";
    }
    while (*String != 0) {
        PrintfOutChar(Output, *String++);
    }
}

static void EmitUnsignedLongLong(FmtOut *Output, unsigned long long Value,
                                 unsigned Base, int Uppercase, int ZeroPad,
                                 int FieldWidth) {
    char Digits[64];
    int DigitCount = 0;
    const char *Alphabet =
        Uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned long long Remaining = Value;
    int Pad;

    if (Base < 2 || Base > 16) {
        return;
    }
    if (Remaining == 0) {
        Digits[DigitCount++] = '0';
    } else {
        while (Remaining > 0 && DigitCount < (int)sizeof(Digits)) {
            Digits[DigitCount++] = Alphabet[Remaining % Base];
            Remaining /= Base;
        }
    }
    if (ZeroPad && FieldWidth > DigitCount) {
        for (Pad = 0; Pad < FieldWidth - DigitCount; Pad++) {
            PrintfOutChar(Output, '0');
        }
    }
    while (DigitCount > 0) {
        PrintfOutChar(Output, Digits[--DigitCount]);
    }
}

static void EmitSignedLong(FmtOut *Output, long Value) {
    unsigned long Magnitude;

    if (Value < 0) {
        PrintfOutChar(Output, '-');
        Magnitude = -(unsigned long)Value;
    } else {
        Magnitude = (unsigned long)Value;
    }
    EmitUnsignedLongLong(Output, Magnitude, 10, 0, 0, 0);
}

static void EmitSignedLongLong(FmtOut *Output, long long Value) {
    unsigned long long Magnitude;

    if (Value < 0) {
        PrintfOutChar(Output, '-');
        Magnitude = -(unsigned long long)Value;
    } else {
        Magnitude = (unsigned long long)Value;
    }
    EmitUnsignedLongLong(Output, Magnitude, 10, 0, 0, 0);
}

static unsigned long long PullUnsigned(va_list Arguments, int LengthKind) {
    if (LengthKind == LengthLongLong) {
        return va_arg(Arguments, unsigned long long);
    }
    if (LengthKind == LengthLong || LengthKind == LengthSize) {
        return va_arg(Arguments, unsigned long);
    }
    return va_arg(Arguments, unsigned);
}

static void EmitConversion(FmtOut *Output, char Specifier, int LengthKind,
                           int ZeroPad, int FieldWidth, int Precision,
                           va_list Arguments) {
    char FloatBuffer[96];
    double FloatValue;
    void *Pointer;
    unsigned long long UnsignedValue;
    unsigned Base;

    if (Specifier == '%') {
        PrintfOutChar(Output, '%');
        return;
    }
    if (Specifier == 'c') {
        PrintfOutChar(Output, (char)va_arg(Arguments, int));
        return;
    }
    if (Specifier == 's') {
        PrintfOutStr(Output, va_arg(Arguments, const char *));
        return;
    }
    if (Specifier == 'd' || Specifier == 'i') {
        if (LengthKind == LengthLongLong) {
            EmitSignedLongLong(Output, va_arg(Arguments, long long));
        } else if (LengthKind == LengthLong || LengthKind == LengthSize) {
            EmitSignedLong(Output, va_arg(Arguments, long));
        } else {
            EmitSignedLong(Output, (long)va_arg(Arguments, int));
        }
        return;
    }
    if (Specifier == 'u' || Specifier == 'o' || Specifier == 'x' ||
        Specifier == 'X') {
        Base = 10;
        if (Specifier == 'o') {
            Base = 8;
        } else if (Specifier == 'x' || Specifier == 'X') {
            Base = 16;
        }
        UnsignedValue = PullUnsigned(Arguments, LengthKind);
        EmitUnsignedLongLong(Output, UnsignedValue, Base, Specifier == 'X',
                             ZeroPad, FieldWidth);
        return;
    }
    if (Specifier == 'f' || Specifier == 'F' || Specifier == 'e' ||
        Specifier == 'E' || Specifier == 'g' || Specifier == 'G') {
        FloatValue = va_arg(Arguments, double);
        PrintfFormatDouble(FloatBuffer, sizeof(FloatBuffer), FloatValue,
                           Specifier, Precision);
        PrintfOutStr(Output, FloatBuffer);
        return;
    }
    if (Specifier == 'p') {
        Pointer = va_arg(Arguments, void *);
        PrintfOutStr(Output, "0x");
        EmitUnsignedLongLong(Output, (unsigned long)(uintptr_t)Pointer, 16, 0,
                             0, 0);
        return;
    }
    PrintfOutChar(Output, '%');
    PrintfOutChar(Output, Specifier);
}

int PrintfFormat(FmtOut *Output, const char *Format, va_list Arguments) {
    const char *Cursor;

    if (!Format) {
        return 0;
    }
    for (Cursor = Format; *Cursor != 0; Cursor++) {
        int ZeroPad;
        int FieldWidth;
        int Precision;
        int LengthKind;

        if (*Cursor != '%') {
            PrintfOutChar(Output, *Cursor);
            continue;
        }
        Cursor++;
        if (*Cursor == 0) {
            break;
        }
        ZeroPad = 0;
        FieldWidth = 0;
        Precision = -1;
        LengthKind = LengthDefault;
        if (*Cursor == '0') {
            ZeroPad = 1;
            Cursor++;
        }
        while (*Cursor >= '0' && *Cursor <= '9') {
            FieldWidth = FieldWidth * 10 + (*Cursor - '0');
            Cursor++;
        }
        if (*Cursor == '.') {
            Precision = 0;
            Cursor++;
            while (*Cursor >= '0' && *Cursor <= '9') {
                Precision = Precision * 10 + (*Cursor - '0');
                Cursor++;
            }
        }
        if (*Cursor == 'l') {
            Cursor++;
            if (*Cursor == 'l') {
                LengthKind = LengthLongLong;
                Cursor++;
            } else {
                LengthKind = LengthLong;
            }
        } else if (*Cursor == 'z') {
            LengthKind = LengthSize;
            Cursor++;
        }
        if (*Cursor == 0) {
            break;
        }
        EmitConversion(Output, *Cursor, LengthKind, ZeroPad, FieldWidth,
                       Precision, Arguments);
    }
    return (int)Output->Position;
}
