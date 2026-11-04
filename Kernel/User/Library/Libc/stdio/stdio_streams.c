/*
 * stdio_streams.c — stdin / stdout / stderr（全称下划线）
 */
#include <stdio.h>
#include "stdio_private.h"

static FILE standard_input_stream = {
    .file_descriptor = 0,
    .flags = FILE_FLAG_READ | FILE_FLAG_STATIC
};
static FILE standard_output_stream = {
    .file_descriptor = 1,
    .flags = FILE_FLAG_WRITE | FILE_FLAG_STATIC
};
static FILE standard_error_stream = {
    .file_descriptor = 2,
    .flags = FILE_FLAG_WRITE | FILE_FLAG_STATIC
};

FILE *stdin = &standard_input_stream;
FILE *stdout = &standard_output_stream;
FILE *stderr = &standard_error_stream;
