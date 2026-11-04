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

struct toy_file {
    int file_descriptor;
    unsigned flags;
    int end_of_file;
    int error;
    int has_ungotten;
    unsigned char ungotten;
    unsigned char write_buffer[STDIO_BUFFER_SIZE];
    size_t write_length;
};

int stdio_flush_write(struct toy_file *stream);

typedef int (*blocks_scan_get_character)(void *context);
typedef void (*blocks_scan_unget_character)(void *context, int character);

int blocks_scan_peek_character(blocks_scan_get_character get_character,
                            blocks_scan_unget_character unget_character,
                            void *context);
void blocks_scan_skip_spaces(blocks_scan_get_character get_character,
                          blocks_scan_unget_character unget_character,
                          void *context);
int blocks_scan_collect_integer_token(blocks_scan_get_character get_character,
                                  blocks_scan_unget_character unget_character,
                                  void *context, char *buffer, size_t capacity,
                                  int base);
int blocks_scan_collect_float_token(blocks_scan_get_character get_character,
                                blocks_scan_unget_character unget_character,
                                void *context, char *buffer, size_t capacity);

/* 成功：已赋值个数；输入失败且尚未赋值：-1 */
int blocks_scan_format(blocks_scan_get_character get_character,
                     blocks_scan_unget_character unget_character, void *context,
                     const char *format, va_list arguments);

#endif
