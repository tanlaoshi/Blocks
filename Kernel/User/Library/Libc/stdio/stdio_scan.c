/*
 * stdio_scan.c — fgetc / fgets / getchar / sscanf / scanf / fscanf
 */
#include <stdarg.h>
#include <stdio.h>

#include "stdio_private.h"

typedef struct {
    const char *cursor;
    const char *begin;
} scan_string_context;

static int scan_string_get_character(void *context) {
    scan_string_context *state = (scan_string_context *)context;

    if (!state || !state->cursor || !*state->cursor) {
        return EOF;
    }
    return (unsigned char)*state->cursor++;
}

static void scan_string_unget_character(void *context, int character) {
    scan_string_context *state = (scan_string_context *)context;

    (void)character;
    if (!state || !state->cursor || state->cursor <= state->begin) {
        return;
    }
    state->cursor--;
}

static int scan_file_get_character(void *context) {
    return fgetc((FILE *)context);
}

static void scan_file_unget_character(void *context, int character) {
    if (character != EOF) {
        (void)ungetc(character, (FILE *)context);
    }
}

int fgetc(FILE *fp) {
    unsigned char byte;
    size_t got;

    if (!fp) {
        return EOF;
    }
    got = fread(&byte, 1, 1, fp);
    if (got != 1) {
        return EOF;
    }
    return (int)byte;
}

int getchar(void) {
    return fgetc(stdin);
}

char *fgets(char *buffer, int size, FILE *file) {
    int index;
    int character;

    if (!buffer || size <= 0 || !file) {
        return 0;
    }
    for (index = 0; index < size - 1; index++) {
        character = fgetc(file);
        if (character == EOF) {
            if (index == 0) {
                return 0;
            }
            break;
        }
        buffer[index] = (char)character;
        if (character == '\n') {
            index++;
            break;
        }
    }
    buffer[index] = 0;
    return buffer;
}

int sscanf(const char *string, const char *format, ...) {
    va_list arguments;
    scan_string_context state;
    int assigned;

    if (!string || !format) {
        return EOF;
    }
    state.begin = string;
    state.cursor = string;
    va_start(arguments, format);
    assigned = blocks_scan_format(scan_string_get_character, scan_string_unget_character,
                                &state, format, arguments);
    va_end(arguments);
    return assigned;
}

int scanf(const char *format, ...) {
    va_list arguments;
    int assigned;

    if (!format) {
        return EOF;
    }
    va_start(arguments, format);
    assigned = blocks_scan_format(scan_file_get_character, scan_file_unget_character,
                                stdin, format, arguments);
    va_end(arguments);
    return assigned;
}

int fscanf(FILE *file, const char *format, ...) {
    va_list arguments;
    int assigned;

    if (!file || !format) {
        return EOF;
    }
    va_start(arguments, format);
    assigned = blocks_scan_format(scan_file_get_character, scan_file_unget_character, file,
                                format, arguments);
    va_end(arguments);
    return assigned;
}
