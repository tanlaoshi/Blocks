/*
 * string.c — 核心 C 字符串 / 内存块（展览级命名；函数名仍为 ISO）
 */
#include <stdlib.h>
#include <string.h>

size_t strlen(const char *String) {
    size_t Length = 0;

    if (!String) {
        return 0;
    }
    while (String[Length] != 0) {
        Length++;
    }
    return Length;
}

int strcmp(const char *Left, const char *Right) {
    if (!Left) {
        Left = "";
    }
    if (!Right) {
        Right = "";
    }
    while (*Left != 0 && *Left == *Right) {
        Left++;
        Right++;
    }
    return (unsigned char)*Left - (unsigned char)*Right;
}

int strncmp(const char *Left, const char *Right, size_t MaxLength) {
    size_t Index;

    if (!Left) {
        Left = "";
    }
    if (!Right) {
        Right = "";
    }
    for (Index = 0; Index < MaxLength; Index++) {
        unsigned char LeftByte = (unsigned char)Left[Index];
        unsigned char RightByte = (unsigned char)Right[Index];

        if (LeftByte != RightByte || LeftByte == 0) {
            return LeftByte - RightByte;
        }
    }
    return 0;
}

char *strcpy(char *Destination, const char *Source) {
    char *Cursor;

    if (!Destination) {
        return Destination;
    }
    Cursor = Destination;
    if (!Source) {
        *Cursor = 0;
        return Destination;
    }
    while ((*Cursor++ = *Source++) != 0) {
    }
    return Destination;
}

char *strncpy(char *Destination, const char *Source, size_t MaxLength) {
    size_t Index;

    if (!Destination) {
        return Destination;
    }
    if (!Source) {
        Source = "";
    }
    for (Index = 0; Index < MaxLength && Source[Index] != 0; Index++) {
        Destination[Index] = Source[Index];
    }
    for (; Index < MaxLength; Index++) {
        Destination[Index] = 0;
    }
    return Destination;
}

char *strcat(char *Destination, const char *Source) {
    char *Cursor;

    if (!Destination) {
        return Destination;
    }
    Cursor = Destination;
    while (*Cursor != 0) {
        Cursor++;
    }
    if (!Source) {
        *Cursor = 0;
        return Destination;
    }
    while ((*Cursor++ = *Source++) != 0) {
    }
    return Destination;
}

char *strdup(const char *String) {
    size_t ByteCount;
    char *Copy;

    if (!String) {
        return 0;
    }
    ByteCount = strlen(String) + 1;
    Copy = (char *)malloc(ByteCount);
    if (!Copy) {
        return 0;
    }
    memcpy(Copy, String, ByteCount);
    return Copy;
}

char *strchr(const char *String, int Character) {
    unsigned char Wanted = (unsigned char)Character;

    if (!String) {
        return 0;
    }
    for (;;) {
        if ((unsigned char)*String == Wanted) {
            return (char *)String;
        }
        if (*String == 0) {
            return 0;
        }
        String++;
    }
}

char *strstr(const char *Haystack, const char *Needle) {
    size_t NeedleLength;
    size_t Index;

    if (!Haystack) {
        return 0;
    }
    if (!Needle || Needle[0] == 0) {
        return (char *)Haystack;
    }
    NeedleLength = strlen(Needle);
    for (Index = 0; Haystack[Index] != 0; Index++) {
        if (strncmp(Haystack + Index, Needle, NeedleLength) == 0) {
            return (char *)(Haystack + Index);
        }
    }
    return 0;
}

void *memcpy(void *Destination, const void *Source, size_t ByteCount) {
    unsigned char *DestinationBytes = (unsigned char *)Destination;
    const unsigned char *SourceBytes = (const unsigned char *)Source;
    size_t Index;

    for (Index = 0; Index < ByteCount; Index++) {
        DestinationBytes[Index] = SourceBytes[Index];
    }
    return Destination;
}

void *memmove(void *Destination, const void *Source, size_t ByteCount) {
    unsigned char *DestinationBytes = (unsigned char *)Destination;
    const unsigned char *SourceBytes = (const unsigned char *)Source;
    size_t Index;

    if (DestinationBytes == SourceBytes || ByteCount == 0) {
        return Destination;
    }
    if (DestinationBytes < SourceBytes) {
        for (Index = 0; Index < ByteCount; Index++) {
            DestinationBytes[Index] = SourceBytes[Index];
        }
    } else {
        for (Index = ByteCount; Index > 0; Index--) {
            DestinationBytes[Index - 1] = SourceBytes[Index - 1];
        }
    }
    return Destination;
}

void *memset(void *Destination, int ByteValue, size_t ByteCount) {
    unsigned char *DestinationBytes = (unsigned char *)Destination;
    size_t Index;

    for (Index = 0; Index < ByteCount; Index++) {
        DestinationBytes[Index] = (unsigned char)ByteValue;
    }
    return Destination;
}

int memcmp(const void *Left, const void *Right, size_t ByteCount) {
    const unsigned char *LeftBytes = (const unsigned char *)Left;
    const unsigned char *RightBytes = (const unsigned char *)Right;
    size_t Index;

    for (Index = 0; Index < ByteCount; Index++) {
        if (LeftBytes[Index] != RightBytes[Index]) {
            return LeftBytes[Index] - RightBytes[Index];
        }
    }
    return 0;
}
