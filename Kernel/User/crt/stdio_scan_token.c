/*
 * stdio_scan_token.c — scan 引擎：空白跳过与整数/浮点词法收集
 */
#include <ctype.h>
#include <stdio.h>

#include "stdio_private.h"

int BlocksScanPeekCharacter(BlocksScanGetCharacter GetCharacter,
                            BlocksScanUngetCharacter UngetCharacter,
                            void *Context) {
    int Character = GetCharacter(Context);

    if (Character != EOF) {
        UngetCharacter(Context, Character);
    }
    return Character;
}

void BlocksScanSkipSpaces(BlocksScanGetCharacter GetCharacter,
                          BlocksScanUngetCharacter UngetCharacter,
                          void *Context) {
    int Character;

    for (;;) {
        Character = GetCharacter(Context);
        if (Character == EOF) {
            return;
        }
        if (!isspace((unsigned char)Character)) {
            UngetCharacter(Context, Character);
            return;
        }
    }
}

static int DigitValueForBase(int Character, int Base) {
    int Value;

    if (Character >= '0' && Character <= '9') {
        Value = Character - '0';
    } else if (Character >= 'a' && Character <= 'z') {
        Value = Character - 'a' + 10;
    } else if (Character >= 'A' && Character <= 'Z') {
        Value = Character - 'A' + 10;
    } else {
        return -1;
    }
    if (Value >= Base) {
        return -1;
    }
    return Value;
}

int BlocksScanCollectIntegerToken(BlocksScanGetCharacter GetCharacter,
                                  BlocksScanUngetCharacter UngetCharacter,
                                  void *Context, char *Buffer, size_t Capacity,
                                  int Base) {
    size_t Length = 0;
    int Character;
    int SeenDigit = 0;
    int CollectBase = Base;

    if (Capacity == 0 || Base < 0 || Base == 1 || Base > 36) {
        return 0;
    }
    Character = GetCharacter(Context);
    if (Character == '+' || Character == '-') {
        Buffer[Length++] = (char)Character;
        Character = GetCharacter(Context);
    }
    if (Base == 0) {
        if (Character == '0') {
            Buffer[Length++] = (char)Character;
            Character = BlocksScanPeekCharacter(GetCharacter, UngetCharacter,
                                               Context);
            if (Character == 'x' || Character == 'X') {
                Character = GetCharacter(Context);
                Buffer[Length++] = (char)Character;
                CollectBase = 16;
                Character = GetCharacter(Context);
            } else {
                CollectBase = 8;
                Character = GetCharacter(Context);
                SeenDigit = 1;
            }
        } else {
            CollectBase = 10;
        }
    } else if (Base == 16 && Character == '0') {
        Buffer[Length++] = (char)Character;
        Character = BlocksScanPeekCharacter(GetCharacter, UngetCharacter, Context);
        if (Character == 'x' || Character == 'X') {
            Character = GetCharacter(Context);
            Buffer[Length++] = (char)Character;
            Character = GetCharacter(Context);
        } else {
            SeenDigit = 1;
            Character = GetCharacter(Context);
        }
    }
    while (Character != EOF && Length < Capacity - 1) {
        if (DigitValueForBase(Character, CollectBase) < 0) {
            break;
        }
        Buffer[Length++] = (char)Character;
        SeenDigit = 1;
        Character = GetCharacter(Context);
    }
    if (Character != EOF) {
        UngetCharacter(Context, Character);
    }
    Buffer[Length] = 0;
    return SeenDigit;
}

int BlocksScanCollectFloatToken(BlocksScanGetCharacter GetCharacter,
                                BlocksScanUngetCharacter UngetCharacter,
                                void *Context, char *Buffer, size_t Capacity) {
    size_t Length = 0;
    int Character;
    int SeenDigit = 0;
    int SeenDot = 0;

    if (Capacity == 0) {
        return 0;
    }
    Character = GetCharacter(Context);
    if (Character == '+' || Character == '-') {
        Buffer[Length++] = (char)Character;
        Character = GetCharacter(Context);
    }
    while (Character != EOF && Length < Capacity - 1) {
        if (isdigit((unsigned char)Character)) {
            Buffer[Length++] = (char)Character;
            SeenDigit = 1;
            Character = GetCharacter(Context);
            continue;
        }
        if (Character == '.' && !SeenDot) {
            Buffer[Length++] = (char)Character;
            SeenDot = 1;
            Character = GetCharacter(Context);
            continue;
        }
        break;
    }
    if ((Character == 'e' || Character == 'E') && SeenDigit &&
        Length < Capacity - 1) {
        Buffer[Length++] = (char)Character;
        Character = GetCharacter(Context);
        if ((Character == '+' || Character == '-') && Length < Capacity - 1) {
            Buffer[Length++] = (char)Character;
            Character = GetCharacter(Context);
        }
        while (isdigit((unsigned char)Character) && Length < Capacity - 1) {
            Buffer[Length++] = (char)Character;
            Character = GetCharacter(Context);
        }
    }
    if (Character != EOF) {
        UngetCharacter(Context, Character);
    }
    Buffer[Length] = 0;
    return SeenDigit;
}
