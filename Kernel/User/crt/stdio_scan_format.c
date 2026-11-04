/*
 * stdio_scan_format.c — 流式 scan 格式分派（展览级铺平）
 *
 * 认：%d %i %u %ld %lu %x %X %o %c %s %f %lf %%；空白；字面量。
 * 词法收集见 stdio_scan_token.c；按字符取还，可跨行、可留下半行。
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "stdio_private.h"

static int FailOrPartial(int Assigned) {
    return Assigned == 0 ? -1 : Assigned;
}

static int ScanIntegerConversion(BlocksScanGetCharacter GetCharacter,
                                 BlocksScanUngetCharacter UngetCharacter,
                                 void *Context, char Specifier, int LengthLong,
                                 va_list Arguments, char *Token,
                                 size_t TokenCapacity) {
    char *ParseEnd = 0;
    int Base;

    BlocksScanSkipSpaces(GetCharacter, UngetCharacter, Context);
    if (Specifier == 'x' || Specifier == 'X') {
        Base = 16;
    } else if (Specifier == 'o') {
        Base = 8;
    } else if (Specifier == 'i') {
        Base = 0;
    } else {
        Base = 10;
    }
    if (!BlocksScanCollectIntegerToken(GetCharacter, UngetCharacter, Context,
                                       Token, TokenCapacity, Base)) {
        return 0;
    }
    if (Specifier == 'u' || Specifier == 'x' || Specifier == 'X' ||
        Specifier == 'o') {
        unsigned long Value =
            strtoul(Token, &ParseEnd, Base == 0 ? 0 : Base);

        if (ParseEnd == Token) {
            return 0;
        }
        if (LengthLong) {
            unsigned long *Destination = va_arg(Arguments, unsigned long *);

            if (Destination) {
                *Destination = Value;
            }
        } else {
            unsigned *Destination = va_arg(Arguments, unsigned *);

            if (Destination) {
                *Destination = (unsigned)Value;
            }
        }
        return 1;
    }
    {
        long Value = strtol(Token, &ParseEnd, Base);

        if (ParseEnd == Token) {
            return 0;
        }
        if (LengthLong) {
            long *Destination = va_arg(Arguments, long *);

            if (Destination) {
                *Destination = Value;
            }
        } else {
            int *Destination = va_arg(Arguments, int *);

            if (Destination) {
                *Destination = (int)Value;
            }
        }
    }
    return 1;
}

static int ScanFloatConversion(BlocksScanGetCharacter GetCharacter,
                               BlocksScanUngetCharacter UngetCharacter,
                               void *Context, int LengthLong, va_list Arguments,
                               char *Token, size_t TokenCapacity) {
    char *ParseEnd = 0;
    double Value;

    BlocksScanSkipSpaces(GetCharacter, UngetCharacter, Context);
    if (!BlocksScanCollectFloatToken(GetCharacter, UngetCharacter, Context, Token,
                                     TokenCapacity)) {
        return 0;
    }
    Value = strtod(Token, &ParseEnd);
    if (ParseEnd == Token) {
        return 0;
    }
    if (LengthLong) {
        double *Destination = va_arg(Arguments, double *);

        if (Destination) {
            *Destination = Value;
        }
    } else {
        float *Destination = va_arg(Arguments, float *);

        if (Destination) {
            *Destination = (float)Value;
        }
    }
    return 1;
}

static int ScanCharacterConversion(BlocksScanGetCharacter GetCharacter,
                                   void *Context, va_list Arguments) {
    char *Destination = va_arg(Arguments, char *);
    int Character = GetCharacter(Context);

    if (Character == EOF) {
        return 0;
    }
    if (Destination) {
        *Destination = (char)Character;
    }
    return 1;
}

static int ScanStringConversion(BlocksScanGetCharacter GetCharacter,
                                BlocksScanUngetCharacter UngetCharacter,
                                void *Context, va_list Arguments) {
    char *Destination = va_arg(Arguments, char *);
    size_t Length = 0;
    int Character;

    BlocksScanSkipSpaces(GetCharacter, UngetCharacter, Context);
    Character = GetCharacter(Context);
    if (Character == EOF) {
        return 0;
    }
    while (Character != EOF && !isspace((unsigned char)Character)) {
        if (Destination) {
            Destination[Length] = (char)Character;
        }
        Length++;
        Character = GetCharacter(Context);
    }
    if (Character != EOF) {
        UngetCharacter(Context, Character);
    }
    if (Destination) {
        Destination[Length] = 0;
    }
    return 1;
}

static int MatchLiteralPercent(BlocksScanGetCharacter GetCharacter,
                               BlocksScanUngetCharacter UngetCharacter,
                               void *Context) {
    int Character = GetCharacter(Context);

    if (Character != '%') {
        if (Character != EOF) {
            UngetCharacter(Context, Character);
        }
        return 0;
    }
    return 1;
}

static int MatchLiteralCharacter(BlocksScanGetCharacter GetCharacter,
                                 BlocksScanUngetCharacter UngetCharacter,
                                 void *Context, char Expected) {
    int Character = GetCharacter(Context);

    if (Character != (unsigned char)Expected) {
        if (Character != EOF) {
            UngetCharacter(Context, Character);
        }
        return Character == EOF ? -1 : 0;
    }
    return 1;
}

int BlocksScanFormat(BlocksScanGetCharacter GetCharacter,
                     BlocksScanUngetCharacter UngetCharacter, void *Context,
                     const char *Format, va_list Arguments) {
    const char *FormatCursor = Format;
    int Assigned = 0;
    int LengthLong;
    char Token[128];
    int ConversionSucceeded;

    if (!GetCharacter || !UngetCharacter || !Format) {
        return -1;
    }
    while (*FormatCursor != 0) {
        if (*FormatCursor == '%') {
            FormatCursor++;
            LengthLong = 0;
            if (*FormatCursor == 'l') {
                LengthLong = 1;
                FormatCursor++;
            }
            if (*FormatCursor == 'd' || *FormatCursor == 'i' ||
                *FormatCursor == 'u' || *FormatCursor == 'x' ||
                *FormatCursor == 'X' || *FormatCursor == 'o') {
                ConversionSucceeded = ScanIntegerConversion(
                    GetCharacter, UngetCharacter, Context, *FormatCursor,
                    LengthLong, Arguments, Token, sizeof(Token));
                if (!ConversionSucceeded) {
                    return FailOrPartial(Assigned);
                }
                Assigned++;
                FormatCursor++;
                continue;
            }
            if (*FormatCursor == 'f') {
                ConversionSucceeded = ScanFloatConversion(
                    GetCharacter, UngetCharacter, Context, LengthLong, Arguments,
                    Token, sizeof(Token));
                if (!ConversionSucceeded) {
                    return FailOrPartial(Assigned);
                }
                Assigned++;
                FormatCursor++;
                continue;
            }
            if (*FormatCursor == 'c') {
                ConversionSucceeded =
                    ScanCharacterConversion(GetCharacter, Context, Arguments);
                if (!ConversionSucceeded) {
                    return FailOrPartial(Assigned);
                }
                Assigned++;
                FormatCursor++;
                continue;
            }
            if (*FormatCursor == 's') {
                ConversionSucceeded = ScanStringConversion(
                    GetCharacter, UngetCharacter, Context, Arguments);
                if (!ConversionSucceeded) {
                    return FailOrPartial(Assigned);
                }
                Assigned++;
                FormatCursor++;
                continue;
            }
            if (*FormatCursor == '%') {
                if (!MatchLiteralPercent(GetCharacter, UngetCharacter, Context)) {
                    return Assigned;
                }
                FormatCursor++;
                continue;
            }
            return Assigned;
        }
        if (isspace((unsigned char)*FormatCursor)) {
            while (isspace((unsigned char)*FormatCursor)) {
                FormatCursor++;
            }
            BlocksScanSkipSpaces(GetCharacter, UngetCharacter, Context);
            continue;
        }
        ConversionSucceeded = MatchLiteralCharacter(
            GetCharacter, UngetCharacter, Context, *FormatCursor);
        if (ConversionSucceeded < 0) {
            return FailOrPartial(Assigned);
        }
        if (ConversionSucceeded == 0) {
            return Assigned;
        }
        FormatCursor++;
    }
    return Assigned;
}
