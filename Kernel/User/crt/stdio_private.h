/*
 * stdio_private.h — FILE 内部（仅 crt/stdio*.c）
 */
#ifndef STDIO_PRIVATE_H
#define STDIO_PRIVATE_H

#include <stddef.h>

#define FF_READ   1u
#define FF_WRITE  2u
#define FF_STATIC 4u /* stdin/out/err：勿 free / 勿关 fd */
#define STDIO_BUF_SIZE 256

struct ToyFile {
    int fd;
    unsigned flags;
    int eof;
    int err;
    int has_unget;
    unsigned char unget;
    unsigned char wbuf[STDIO_BUF_SIZE];
    size_t wlen;
};

int StdioFlushWrite(struct ToyFile *fp);

#endif
