/*
 * stdio.c — fopen / fflush / fclose / fseek / ungetc（libc-stdio-buf）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Blocks/File.h>
#include <errno.h>
#include <dirent.h>
#include "stdio_private.h"

int StdioFlushWrite(FILE *fp) {
    size_t Off = 0;
    ssize_t N;

    if (!fp || fp->wlen == 0) {
        return 0;
    }
    while (Off < fp->wlen) {
        N = FileWrite(fp->fd, fp->wbuf + Off, fp->wlen - Off);
        if (N <= 0) {
            fp->err = 1;
            return -1;
        }
        Off += (size_t)N;
    }
    fp->wlen = 0;
    return 0;
}

static int ParseMode(const char *mode, unsigned *OutFlags, int *OutAppend, int *OutExist) {
    unsigned Flags = 0;
    int Append = 0;
    int Plus = 0;
    char Primary = 0;
    const char *P;

    if (!mode || !mode[0] || !OutFlags || !OutAppend || !OutExist) {
        return -1;
    }
    for (P = mode; *P; P++) {
        if (*P == 'b') {
            continue;
        }
        if (*P == '+') {
            Plus = 1;
            continue;
        }
        if (*P == 'r' || *P == 'w' || *P == 'a') {
            if (Primary) {
                return -1;
            }
            Primary = *P;
            continue;
        }
        return -1;
    }
    if (!Primary) {
        return -1;
    }
    if (Primary == 'r') {
        Flags = FF_READ;
    } else if (Primary == 'w') {
        Flags = FF_WRITE;
    } else {
        Flags = FF_WRITE;
        Append = 1;
    }
    if (Plus) {
        Flags = FF_READ | FF_WRITE;
    }
    *OutFlags = Flags;
    *OutAppend = Append;
    *OutExist = (Primary == 'r');
    return 0;
}

FILE *fopen(const char *path, const char *mode) {
    unsigned Flags;
    int Append;
    int MustExist;
    int Fd;
    FILE *Fp;
    FS_FILE_STAT St;

    if (ParseMode(mode, &Flags, &Append, &MustExist) < 0 || !path || !path[0]) {
        errno = EINVAL;
        return 0;
    }
    if (MustExist) {
        if (FileStat(path, &St) < 0) {
            errno = ENOENT;
            return 0;
        }
        if (St.Attr & FS_ATTR_DIR) {
            errno = EINVAL;
            return 0;
        }
    }
    Fd = FileOpen(path);
    if (Fd < 0) {
        return 0;
    }
    if (Append) {
        if (FileSeek(Fd, 0, SEEK_END) < 0) {
            FileClose(Fd);
            return 0;
        }
    }
    Fp = (FILE *)malloc(sizeof(*Fp));
    if (!Fp) {
        FileClose(Fd);
        errno = ENOMEM;
        return 0;
    }
    memset(Fp, 0, sizeof(*Fp));
    Fp->fd = Fd;
    Fp->flags = Flags;
    return Fp;
}

int fflush(FILE *fp) {
    if (!fp) {
        return 0;
    }
    if (StdioFlushWrite(fp) < 0) {
        return EOF;
    }
    return 0;
}

int fclose(FILE *fp) {
    int Rc;

    if (!fp) {
        errno = EINVAL;
        return EOF;
    }
    if (StdioFlushWrite(fp) < 0) {
        if ((fp->flags & FF_STATIC) == 0) {
            FileClose(fp->fd);
            free(fp);
        }
        return EOF;
    }
    if (fp->flags & FF_STATIC) {
        return 0;
    }
    Rc = FileClose(fp->fd);
    free(fp);
    return Rc;
}

int fseek(FILE *fp, long offset, int whence) {
    if (!fp) {
        errno = EINVAL;
        return -1;
    }
    if (StdioFlushWrite(fp) < 0) {
        return -1;
    }
    fp->has_unget = 0;
    if (FileSeek(fp->fd, (off_t)offset, whence) < 0) {
        fp->err = 1;
        return -1;
    }
    fp->eof = 0;
    return 0;
}

long ftell(FILE *fp) {
    off_t Pos;

    if (!fp) {
        errno = EINVAL;
        return -1;
    }
    if (StdioFlushWrite(fp) < 0) {
        return -1;
    }
    Pos = FileSeek(fp->fd, 0, SEEK_CUR);
    if (Pos < 0) {
        fp->err = 1;
        return -1;
    }
    if (fp->has_unget && Pos > 0) {
        return (long)Pos - 1;
    }
    return (long)Pos;
}

int ungetc(int c, FILE *fp) {
    if (!fp || c == EOF || fp->has_unget) {
        return EOF;
    }
    fp->unget = (unsigned char)c;
    fp->has_unget = 1;
    fp->eof = 0;
    return (int)fp->unget;
}

int feof(FILE *fp) {
    return fp ? fp->eof : 0;
}

int ferror(FILE *fp) {
    return fp ? fp->err : 1;
}

int fileno(FILE *fp) {
    if (!fp) {
        errno = EINVAL;
        return -1;
    }
    return fp->fd;
}
