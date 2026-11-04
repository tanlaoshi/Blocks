/*
 * stdio_streams.c — stdin / stdout / stderr（hosted-stdio-streams）
 */
#include <stdio.h>
#include "stdio_private.h"

static FILE gStdIn = {
    .fd = 0,
    .flags = FF_READ | FF_STATIC
};
static FILE gStdOut = {
    .fd = 1,
    .flags = FF_WRITE | FF_STATIC
};
static FILE gStdErr = {
    .fd = 2,
    .flags = FF_WRITE | FF_STATIC
};

FILE *stdin = &gStdIn;
FILE *stdout = &gStdOut;
FILE *stderr = &gStdErr;
