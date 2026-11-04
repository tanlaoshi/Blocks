/*
 * stdlib.c — abs / labs / atoi / atol / qsort（展览级命名）
 * malloc → malloc.c；exit → atexit.c；bsearch/rand → stdlib_search.c
 */
#include <ctype.h>
#include <stdlib.h>

int abs(int Value) {
    return Value < 0 ? -Value : Value;
}

long labs(long Value) {
    return Value < 0 ? -Value : Value;
}

int atoi(const char *String) {
    return (int)atol(String);
}

long atol(const char *String) {
    long Sign = 1;
    long Value = 0;

    if (!String) {
        return 0;
    }
    while (isspace((unsigned char)*String)) {
        String++;
    }
    if (*String == '+' || *String == '-') {
        if (*String == '-') {
            Sign = -1;
        }
        String++;
    }
    while (isdigit((unsigned char)*String)) {
        Value = Value * 10 + (*String - '0');
        String++;
    }
    return Sign * Value;
}

static void SwapBytes(unsigned char *Left, unsigned char *Right, size_t ByteCount) {
    size_t Index;

    for (Index = 0; Index < ByteCount; Index++) {
        unsigned char Temporary = Left[Index];

        Left[Index] = Right[Index];
        Right[Index] = Temporary;
    }
}

void qsort(void *Base, size_t ElementCount, size_t ElementSize,
           int (*Compare)(const void *, const void *)) {
    unsigned char *Bytes;
    size_t Outer;
    size_t Inner;

    if (!Base || !Compare || ElementCount < 2 || ElementSize == 0) {
        return;
    }
    Bytes = (unsigned char *)Base;
    /* 教学用插入排序：稳定、无递归栈 */
    for (Outer = 1; Outer < ElementCount; Outer++) {
        for (Inner = Outer; Inner > 0; Inner--) {
            unsigned char *Left = Bytes + (Inner - 1) * ElementSize;
            unsigned char *Right = Bytes + Inner * ElementSize;

            if (Compare(Left, Right) <= 0) {
                break;
            }
            SwapBytes(Left, Right, ElementSize);
        }
    }
}
