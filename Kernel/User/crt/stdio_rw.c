/*
 * stdio_rw.c — fread / fwrite（展览级命名；写缓冲 + ungetc）
 */
#include <stdio.h>
#include <errno.h>
#include <Blocks/File.h>
#include "stdio_private.h"

size_t fread(void *Buffer, size_t ElementSize, size_t ElementCount, FILE *Stream) {
    unsigned char *Output;
    size_t ByteCount;
    size_t Got = 0;
    ssize_t ReadCount;

    if (!Stream || !Buffer || ElementSize == 0 || ElementCount == 0) {
        return 0;
    }
    if ((Stream->Flags & FILE_FLAG_READ) == 0) {
        Stream->Error = 1;
        errno = EINVAL;
        return 0;
    }
    if (StdioFlushWrite(Stream) < 0) {
        return 0;
    }
    if (ElementSize != 0 && ElementCount > ((size_t)-1) / ElementSize) {
        Stream->Error = 1;
        errno = EINVAL;
        return 0;
    }
    ByteCount = ElementSize * ElementCount;
    Output = (unsigned char *)Buffer;
    if (Stream->HasUngotten && ByteCount > 0) {
        Output[0] = Stream->Ungotten;
        Stream->HasUngotten = 0;
        Got = 1;
        Output++;
        ByteCount--;
    }
    if (ByteCount > 0) {
        ReadCount = FileRead(Stream->FileDescriptor, Output, ByteCount);
        if (ReadCount < 0) {
            Stream->Error = 1;
            return Got / ElementSize;
        }
        if ((size_t)ReadCount < ByteCount) {
            Stream->EndOfFile = 1;
        }
        Got += (size_t)ReadCount;
    }
    return Got / ElementSize;
}

size_t fwrite(const void *Buffer, size_t ElementSize, size_t ElementCount,
              FILE *Stream) {
    const unsigned char *Input;
    size_t ByteCount;
    size_t Done = 0;

    if (!Stream || !Buffer || ElementSize == 0 || ElementCount == 0) {
        return 0;
    }
    if ((Stream->Flags & FILE_FLAG_WRITE) == 0) {
        Stream->Error = 1;
        errno = EINVAL;
        return 0;
    }
    if (ElementSize != 0 && ElementCount > ((size_t)-1) / ElementSize) {
        Stream->Error = 1;
        errno = EINVAL;
        return 0;
    }
    ByteCount = ElementSize * ElementCount;
    Input = (const unsigned char *)Buffer;
    while (Done < ByteCount) {
        size_t Room = STDIO_BUFFER_SIZE - Stream->WriteLength;
        size_t Chunk = ByteCount - Done;
        size_t Index;

        if (Room == 0) {
            if (StdioFlushWrite(Stream) < 0) {
                return Done / ElementSize;
            }
            Room = STDIO_BUFFER_SIZE;
        }
        if (Chunk > Room) {
            Chunk = Room;
        }
        for (Index = 0; Index < Chunk; Index++) {
            unsigned char Byte = Input[Done + Index];

            Stream->WriteBuffer[Stream->WriteLength++] = Byte;
            if (Byte == '\n') {
                if (StdioFlushWrite(Stream) < 0) {
                    return (Done + Index + 1) / ElementSize;
                }
            }
        }
        Done += Chunk;
    }
    return Done / ElementSize;
}
