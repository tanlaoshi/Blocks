/*
 * stdio_rw.c — fread / fwrite（带写缓冲 / ungetc）（libc-stdio-buf）
 */
#include <stdio.h>
#include <errno.h>
#include <Blocks/File.h>
#include "stdio_private.h"

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) {
    unsigned char *Out;
    size_t Bytes;
    size_t Got = 0;
    ssize_t N;

    if (!fp || !ptr || size == 0 || nmemb == 0) {
        return 0;
    }
    if ((fp->flags & FF_READ) == 0) {
        fp->err = 1;
        errno = EINVAL;
        return 0;
    }
    if (StdioFlushWrite(fp) < 0) {
        return 0;
    }
    if (size != 0 && nmemb > ((size_t)-1) / size) {
        fp->err = 1;
        errno = EINVAL;
        return 0;
    }
    Bytes = size * nmemb;
    Out = (unsigned char *)ptr;
    if (fp->has_unget && Bytes > 0) {
        Out[0] = fp->unget;
        fp->has_unget = 0;
        Got = 1;
        Out++;
        Bytes--;
    }
    if (Bytes > 0) {
        N = FileRead(fp->fd, Out, Bytes);
        if (N < 0) {
            fp->err = 1;
            return Got / size;
        }
        if ((size_t)N < Bytes) {
            fp->eof = 1;
        }
        Got += (size_t)N;
    }
    return Got / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) {
    const unsigned char *In;
    size_t Bytes;
    size_t Done = 0;

    if (!fp || !ptr || size == 0 || nmemb == 0) {
        return 0;
    }
    if ((fp->flags & FF_WRITE) == 0) {
        fp->err = 1;
        errno = EINVAL;
        return 0;
    }
    if (size != 0 && nmemb > ((size_t)-1) / size) {
        fp->err = 1;
        errno = EINVAL;
        return 0;
    }
    Bytes = size * nmemb;
    In = (const unsigned char *)ptr;
    while (Done < Bytes) {
        size_t Room = STDIO_BUF_SIZE - fp->wlen;
        size_t Chunk = Bytes - Done;
        size_t I;

        if (Room == 0) {
            if (StdioFlushWrite(fp) < 0) {
                return Done / size;
            }
            Room = STDIO_BUF_SIZE;
        }
        if (Chunk > Room) {
            Chunk = Room;
        }
        for (I = 0; I < Chunk; I++) {
            unsigned char C = In[Done + I];
            fp->wbuf[fp->wlen++] = C;
            if (C == '\n') {
                if (StdioFlushWrite(fp) < 0) {
                    return (Done + I + 1) / size;
                }
            }
        }
        Done += Chunk;
    }
    return Done / size;
}
