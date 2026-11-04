/*
 * stdio_scan.c — fgetc / fgets / getchar / sscanf / scanf / fscanf
 */
#include <stdarg.h>
#include <stdio.h>

#include "stdio_private.h"

typedef struct {
    const char *Cursor;
    const char *Begin;
} ScanStringContext;

static int ScanStringGetCharacter(void *Context) {
    ScanStringContext *State = (ScanStringContext *)Context;

    if (!State || !State->Cursor || !*State->Cursor) {
        return EOF;
    }
    return (unsigned char)*State->Cursor++;
}

static void ScanStringUngetCharacter(void *Context, int Character) {
    ScanStringContext *State = (ScanStringContext *)Context;

    (void)Character;
    if (!State || !State->Cursor || State->Cursor <= State->Begin) {
        return;
    }
    State->Cursor--;
}

static int ScanFileGetCharacter(void *Context) {
    return fgetc((FILE *)Context);
}

static void ScanFileUngetCharacter(void *Context, int Character) {
    if (Character != EOF) {
        (void)ungetc(Character, (FILE *)Context);
    }
}

int fgetc(FILE *fp) {
    unsigned char Byte;
    size_t Got;

    if (!fp) {
        return EOF;
    }
    Got = fread(&Byte, 1, 1, fp);
    if (Got != 1) {
        return EOF;
    }
    return (int)Byte;
}

int getchar(void) {
    return fgetc(stdin);
}

char *fgets(char *Buffer, int Size, FILE *File) {
    int Index;
    int Character;

    if (!Buffer || Size <= 0 || !File) {
        return 0;
    }
    for (Index = 0; Index < Size - 1; Index++) {
        Character = fgetc(File);
        if (Character == EOF) {
            if (Index == 0) {
                return 0;
            }
            break;
        }
        Buffer[Index] = (char)Character;
        if (Character == '\n') {
            Index++;
            break;
        }
    }
    Buffer[Index] = 0;
    return Buffer;
}

int sscanf(const char *String, const char *Format, ...) {
    va_list Arguments;
    ScanStringContext State;
    int Assigned;

    if (!String || !Format) {
        return EOF;
    }
    State.Begin = String;
    State.Cursor = String;
    va_start(Arguments, Format);
    Assigned = BlocksScanFormat(ScanStringGetCharacter, ScanStringUngetCharacter,
                                &State, Format, Arguments);
    va_end(Arguments);
    return Assigned;
}

int scanf(const char *Format, ...) {
    va_list Arguments;
    int Assigned;

    if (!Format) {
        return EOF;
    }
    va_start(Arguments, Format);
    Assigned = BlocksScanFormat(ScanFileGetCharacter, ScanFileUngetCharacter,
                                stdin, Format, Arguments);
    va_end(Arguments);
    return Assigned;
}

int fscanf(FILE *File, const char *Format, ...) {
    va_list Arguments;
    int Assigned;

    if (!File || !Format) {
        return EOF;
    }
    va_start(Arguments, Format);
    Assigned = BlocksScanFormat(ScanFileGetCharacter, ScanFileUngetCharacter, File,
                                Format, Arguments);
    va_end(Arguments);
    return Assigned;
}
