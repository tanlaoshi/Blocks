/*
 * stdio_scan_format.c — 流式 scan 格式分派（教学子集）
 *
 * 认：%d %i %u %ld %lu %x %X %o %c %s %f %lf %%；空白；字面量。
 * 词法收集见 stdio_scan_token.c；按字符取还，可跨行、可留下半行。
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "stdio_private.h"

int BlocksScanFormat(BlocksScanGetCharacter GetCharacter,
                     BlocksScanUngetCharacter UngetCharacter, void *Context,
                     const char *Format, va_list Arguments) {
    const char *FormatCursor = Format;
    int Assigned = 0;
    int LengthLong;
    char Token[128];
    int Base;

    if (!GetCharacter || !UngetCharacter || !Format) {
        return -1;
    }
    while (*FormatCursor) {
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
                char *End = 0;

                BlocksScanSkipSpaces(GetCharacter, UngetCharacter, Context);
                if (*FormatCursor == 'x' || *FormatCursor == 'X') {
                    Base = 16;
                } else if (*FormatCursor == 'o') {
                    Base = 8;
                } else if (*FormatCursor == 'i') {
                    Base = 0;
                } else if (*FormatCursor == 'u') {
                    Base = 10;
                } else {
                    Base = 10;
                }
                if (!BlocksScanCollectIntegerToken(GetCharacter, UngetCharacter,
                                                   Context, Token, sizeof(Token),
                                                   Base)) {
                    return Assigned == 0 ? -1 : Assigned;
                }
                if (*FormatCursor == 'u' || *FormatCursor == 'x' ||
                    *FormatCursor == 'X' || *FormatCursor == 'o') {
                    unsigned long Value = strtoul(Token, &End, Base == 0 ? 0 : Base);

                    if (End == Token) {
                        return Assigned == 0 ? -1 : Assigned;
                    }
                    if (LengthLong) {
                        unsigned long *Out = va_arg(Arguments, unsigned long *);

                        if (Out) {
                            *Out = Value;
                        }
                    } else {
                        unsigned *Out = va_arg(Arguments, unsigned *);

                        if (Out) {
                            *Out = (unsigned)Value;
                        }
                    }
                } else {
                    long Value = strtol(Token, &End, Base);

                    if (End == Token) {
                        return Assigned == 0 ? -1 : Assigned;
                    }
                    if (LengthLong) {
                        long *Out = va_arg(Arguments, long *);

                        if (Out) {
                            *Out = Value;
                        }
                    } else {
                        int *Out = va_arg(Arguments, int *);

                        if (Out) {
                            *Out = (int)Value;
                        }
                    }
                }
                Assigned++;
                FormatCursor++;
                continue;
            }
            if (*FormatCursor == 'f') {
                char *End = 0;
                double Value;

                BlocksScanSkipSpaces(GetCharacter, UngetCharacter, Context);
                if (!BlocksScanCollectFloatToken(GetCharacter, UngetCharacter,
                                                 Context, Token, sizeof(Token))) {
                    return Assigned == 0 ? -1 : Assigned;
                }
                Value = strtod(Token, &End);
                if (End == Token) {
                    return Assigned == 0 ? -1 : Assigned;
                }
                if (LengthLong) {
                    double *Out = va_arg(Arguments, double *);

                    if (Out) {
                        *Out = Value;
                    }
                } else {
                    float *Out = va_arg(Arguments, float *);

                    if (Out) {
                        *Out = (float)Value;
                    }
                }
                Assigned++;
                FormatCursor++;
                continue;
            }
            if (*FormatCursor == 'c') {
                char *Out = va_arg(Arguments, char *);
                int Character = GetCharacter(Context);

                if (Character == EOF) {
                    return Assigned == 0 ? -1 : Assigned;
                }
                if (Out) {
                    *Out = (char)Character;
                }
                Assigned++;
                FormatCursor++;
                continue;
            }
            if (*FormatCursor == 's') {
                char *Out = va_arg(Arguments, char *);
                size_t Length = 0;
                int Character;

                BlocksScanSkipSpaces(GetCharacter, UngetCharacter, Context);
                Character = GetCharacter(Context);
                if (Character == EOF) {
                    return Assigned == 0 ? -1 : Assigned;
                }
                while (Character != EOF && !isspace((unsigned char)Character)) {
                    if (Out) {
                        Out[Length] = (char)Character;
                    }
                    Length++;
                    Character = GetCharacter(Context);
                }
                if (Character != EOF) {
                    UngetCharacter(Context, Character);
                }
                if (Out) {
                    Out[Length] = 0;
                }
                Assigned++;
                FormatCursor++;
                continue;
            }
            if (*FormatCursor == '%') {
                int Character = GetCharacter(Context);

                if (Character != '%') {
                    if (Character != EOF) {
                        UngetCharacter(Context, Character);
                    }
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
        {
            int Character = GetCharacter(Context);

            if (Character != (unsigned char)*FormatCursor) {
                if (Character != EOF) {
                    UngetCharacter(Context, Character);
                }
                return Assigned == 0 && Character == EOF ? -1 : Assigned;
            }
        }
        FormatCursor++;
    }
    return Assigned;
}
