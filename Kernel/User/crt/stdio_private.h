/*
 * stdio_private.h — FILE 内部与 scan 引擎（仅 crt/stdio*.c）
 */
#ifndef STDIO_PRIVATE_H
#define STDIO_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

#define FILE_FLAG_READ   1u
#define FILE_FLAG_WRITE  2u
#define FILE_FLAG_STATIC 4u /* stdin/out/err：勿 free / 勿关 fd */
#define STDIO_BUFFER_SIZE 256

struct ToyFile {
    int FileDescriptor;
    unsigned Flags;
    int EndOfFile;
    int Error;
    int HasUngotten;
    unsigned char Ungotten;
    unsigned char WriteBuffer[STDIO_BUFFER_SIZE];
    size_t WriteLength;
};

int StdioFlushWrite(struct ToyFile *Stream);

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
