/*
 * stdio.c — fopen / fflush / fclose / fseek / ungetc（展览级命名）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Blocks/File.h>
#include <errno.h>
#include <dirent.h>
#include "stdio_private.h"

int StdioFlushWrite(FILE *Stream) {
    size_t Offset = 0;
    ssize_t Written;

    if (!Stream || Stream->WriteLength == 0) {
        return 0;
    }
    while (Offset < Stream->WriteLength) {
        Written = FileWrite(Stream->FileDescriptor, Stream->WriteBuffer + Offset,
                            Stream->WriteLength - Offset);
        if (Written <= 0) {
            Stream->Error = 1;
            return -1;
        }
        Offset += (size_t)Written;
    }
    Stream->WriteLength = 0;
    return 0;
}

static int ParseMode(const char *Mode, unsigned *FlagsOut, int *AppendOut,
                     int *MustExistOut) {
    unsigned Flags = 0;
    int Append = 0;
    int Plus = 0;
    char Primary = 0;
    const char *Cursor;

    if (!Mode || Mode[0] == 0 || !FlagsOut || !AppendOut || !MustExistOut) {
        return -1;
    }
    for (Cursor = Mode; *Cursor != 0; Cursor++) {
        if (*Cursor == 'b') {
            continue;
        }
        if (*Cursor == '+') {
            Plus = 1;
            continue;
        }
        if (*Cursor == 'r' || *Cursor == 'w' || *Cursor == 'a') {
            if (Primary != 0) {
                return -1;
            }
            Primary = *Cursor;
            continue;
        }
        return -1;
    }
    if (Primary == 0) {
        return -1;
    }
    if (Primary == 'r') {
        Flags = FILE_FLAG_READ;
    } else if (Primary == 'w') {
        Flags = FILE_FLAG_WRITE;
    } else {
        Flags = FILE_FLAG_WRITE;
        Append = 1;
    }
    if (Plus) {
        Flags = FILE_FLAG_READ | FILE_FLAG_WRITE;
    }
    *FlagsOut = Flags;
    *AppendOut = Append;
    *MustExistOut = (Primary == 'r');
    return 0;
}

FILE *fopen(const char *Path, const char *Mode) {
    unsigned Flags;
    int Append;
    int MustExist;
    int FileDescriptor;
    FILE *Stream;
    FS_FILE_STAT Status;

    if (ParseMode(Mode, &Flags, &Append, &MustExist) < 0 || !Path ||
        Path[0] == 0) {
        errno = EINVAL;
        return 0;
    }
    if (MustExist) {
        if (FileStat(Path, &Status) < 0) {
            errno = ENOENT;
            return 0;
        }
        if (Status.Attr & FS_ATTR_DIR) {
            errno = EINVAL;
            return 0;
        }
    }
    FileDescriptor = FileOpen(Path);
    if (FileDescriptor < 0) {
        return 0;
    }
    if (Append) {
        if (FileSeek(FileDescriptor, 0, SEEK_END) < 0) {
            FileClose(FileDescriptor);
            return 0;
        }
    }
    Stream = (FILE *)malloc(sizeof(*Stream));
    if (!Stream) {
        FileClose(FileDescriptor);
        errno = ENOMEM;
        return 0;
    }
    memset(Stream, 0, sizeof(*Stream));
    Stream->FileDescriptor = FileDescriptor;
    Stream->Flags = Flags;
    return Stream;
}

int fflush(FILE *Stream) {
    if (!Stream) {
        return 0;
    }
    if (StdioFlushWrite(Stream) < 0) {
        return EOF;
    }
    return 0;
}

int fclose(FILE *Stream) {
    int CloseResult;

    if (!Stream) {
        errno = EINVAL;
        return EOF;
    }
    if (StdioFlushWrite(Stream) < 0) {
        if ((Stream->Flags & FILE_FLAG_STATIC) == 0) {
            FileClose(Stream->FileDescriptor);
            free(Stream);
        }
        return EOF;
    }
    if (Stream->Flags & FILE_FLAG_STATIC) {
        return 0;
    }
    CloseResult = FileClose(Stream->FileDescriptor);
    free(Stream);
    return CloseResult;
}

int fseek(FILE *Stream, long Offset, int Whence) {
    if (!Stream) {
        errno = EINVAL;
        return -1;
    }
    if (StdioFlushWrite(Stream) < 0) {
        return -1;
    }
    Stream->HasUngotten = 0;
    if (FileSeek(Stream->FileDescriptor, (off_t)Offset, Whence) < 0) {
        Stream->Error = 1;
        return -1;
    }
    Stream->EndOfFile = 0;
    return 0;
}

long ftell(FILE *Stream) {
    off_t Position;

    if (!Stream) {
        errno = EINVAL;
        return -1;
    }
    if (StdioFlushWrite(Stream) < 0) {
        return -1;
    }
    Position = FileSeek(Stream->FileDescriptor, 0, SEEK_CUR);
    if (Position < 0) {
        Stream->Error = 1;
        return -1;
    }
    if (Stream->HasUngotten && Position > 0) {
        return (long)Position - 1;
    }
    return (long)Position;
}

int ungetc(int Character, FILE *Stream) {
    if (!Stream || Character == EOF || Stream->HasUngotten) {
        return EOF;
    }
    Stream->Ungotten = (unsigned char)Character;
    Stream->HasUngotten = 1;
    Stream->EndOfFile = 0;
    return (int)Stream->Ungotten;
}

int feof(FILE *Stream) {
    return Stream ? Stream->EndOfFile : 0;
}

int ferror(FILE *Stream) {
    return Stream ? Stream->Error : 1;
}

int fileno(FILE *Stream) {
    if (!Stream) {
        errno = EINVAL;
        return -1;
    }
    return Stream->FileDescriptor;
}
