/*
 * stdio_streams.c — stdin / stdout / stderr（展览级命名）
 */
#include <stdio.h>
#include "stdio_private.h"

static FILE StandardInputStream = {
    .FileDescriptor = 0,
    .Flags = FILE_FLAG_READ | FILE_FLAG_STATIC
};
static FILE StandardOutputStream = {
    .FileDescriptor = 1,
    .Flags = FILE_FLAG_WRITE | FILE_FLAG_STATIC
};
static FILE StandardErrorStream = {
    .FileDescriptor = 2,
    .Flags = FILE_FLAG_WRITE | FILE_FLAG_STATIC
};

FILE *stdin = &StandardInputStream;
FILE *stdout = &StandardOutputStream;
FILE *stderr = &StandardErrorStream;
