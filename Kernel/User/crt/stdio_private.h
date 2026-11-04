/*
 * stdio_private.h — FILE 内部与 scan 引擎（仅 crt/stdio*.c）
 */
#ifndef STDIO_PRIVATE_H
#define STDIO_PRIVATE_H

#include <stdarg.h>
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

typedef int (*BlocksScanGetCharacter)(void *Context);
typedef void (*BlocksScanUngetCharacter)(void *Context, int Character);

int BlocksScanPeekCharacter(BlocksScanGetCharacter GetCharacter,
                            BlocksScanUngetCharacter UngetCharacter,
                            void *Context);
void BlocksScanSkipSpaces(BlocksScanGetCharacter GetCharacter,
                          BlocksScanUngetCharacter UngetCharacter,
                          void *Context);
int BlocksScanCollectIntegerToken(BlocksScanGetCharacter GetCharacter,
                                  BlocksScanUngetCharacter UngetCharacter,
                                  void *Context, char *Buffer, size_t Capacity,
                                  int Base);
int BlocksScanCollectFloatToken(BlocksScanGetCharacter GetCharacter,
                                BlocksScanUngetCharacter UngetCharacter,
                                void *Context, char *Buffer, size_t Capacity);

/* 成功：已赋值个数；输入失败且尚未赋值：-1 */
int BlocksScanFormat(BlocksScanGetCharacter GetCharacter,
                     BlocksScanUngetCharacter UngetCharacter, void *Context,
                     const char *Format, va_list Arguments);

#endif
