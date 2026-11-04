/*
 * stdio_scan_format.c — 流式 scan 格式分派（展览级铺平）
 *
 * 认：%d %i %u %ld %lu %x %X %o %c %s %f %lf %%；空白；字面量。
 * 词法收集见 stdio_scan_token.c；按字符取还，可跨行、可留下半行。
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "stdio_private.h"

static int fail_or_partial(int assigned) {
    return assigned == 0 ? -1 : assigned;
}

static int scan_integer_conversion(blocks_scan_get_character get_character,
                                 blocks_scan_unget_character unget_character,
                                 void *context, char specifier, int length_long,
                                 va_list arguments, char *token,
                                 size_t token_capacity) {
    char *parse_end = 0;
    int base;

    blocks_scan_skip_spaces(get_character, unget_character, context);
    if (specifier == 'x' || specifier == 'X') {
        base = 16;
    } else if (specifier == 'o') {
        base = 8;
    } else if (specifier == 'i') {
        base = 0;
    } else {
        base = 10;
    }
    if (!blocks_scan_collect_integer_token(get_character, unget_character, context,
                                       token, token_capacity, base)) {
        return 0;
    }
    if (specifier == 'u' || specifier == 'x' || specifier == 'X' ||
        specifier == 'o') {
        unsigned long value =
            strtoul(token, &parse_end, base == 0 ? 0 : base);

        if (parse_end == token) {
            return 0;
        }
        if (length_long) {
            unsigned long *destination = va_arg(arguments, unsigned long *);

            if (destination) {
                *destination = value;
            }
        } else {
            unsigned *destination = va_arg(arguments, unsigned *);

            if (destination) {
                *destination = (unsigned)value;
            }
        }
        return 1;
    }
    {
        long value = strtol(token, &parse_end, base);

        if (parse_end == token) {
            return 0;
        }
        if (length_long) {
            long *destination = va_arg(arguments, long *);

            if (destination) {
                *destination = value;
            }
        } else {
            int *destination = va_arg(arguments, int *);

            if (destination) {
                *destination = (int)value;
            }
        }
    }
    return 1;
}

static int scan_float_conversion(blocks_scan_get_character get_character,
                               blocks_scan_unget_character unget_character,
                               void *context, int length_long, va_list arguments,
                               char *token, size_t token_capacity) {
    char *parse_end = 0;
    double value;

    blocks_scan_skip_spaces(get_character, unget_character, context);
    if (!blocks_scan_collect_float_token(get_character, unget_character, context, token,
                                     token_capacity)) {
        return 0;
    }
    value = strtod(token, &parse_end);
    if (parse_end == token) {
        return 0;
    }
    if (length_long) {
        double *destination = va_arg(arguments, double *);

        if (destination) {
            *destination = value;
        }
    } else {
        float *destination = va_arg(arguments, float *);

        if (destination) {
            *destination = (float)value;
        }
    }
    return 1;
}

static int scan_character_conversion(blocks_scan_get_character get_character,
                                   void *context, va_list arguments) {
    char *destination = va_arg(arguments, char *);
    int character = get_character(context);

    if (character == EOF) {
        return 0;
    }
    if (destination) {
        *destination = (char)character;
    }
    return 1;
}

static int scan_string_conversion(blocks_scan_get_character get_character,
                                blocks_scan_unget_character unget_character,
                                void *context, va_list arguments) {
    char *destination = va_arg(arguments, char *);
    size_t length = 0;
    int character;

    blocks_scan_skip_spaces(get_character, unget_character, context);
    character = get_character(context);
    if (character == EOF) {
        return 0;
    }
    while (character != EOF && !isspace((unsigned char)character)) {
        if (destination) {
            destination[length] = (char)character;
        }
        length++;
        character = get_character(context);
    }
    if (character != EOF) {
        unget_character(context, character);
    }
    if (destination) {
        destination[length] = 0;
    }
    return 1;
}

static int match_literal_percent(blocks_scan_get_character get_character,
                               blocks_scan_unget_character unget_character,
                               void *context) {
    int character = get_character(context);

    if (character != '%') {
        if (character != EOF) {
            unget_character(context, character);
        }
        return 0;
    }
    return 1;
}

static int match_literal_character(blocks_scan_get_character get_character,
                                 blocks_scan_unget_character unget_character,
                                 void *context, char expected) {
    int character = get_character(context);

    if (character != (unsigned char)expected) {
        if (character != EOF) {
            unget_character(context, character);
        }
        return character == EOF ? -1 : 0;
    }
    return 1;
}

int blocks_scan_format(blocks_scan_get_character get_character,
                     blocks_scan_unget_character unget_character, void *context,
                     const char *format, va_list arguments) {
    const char *format_cursor = format;
    int assigned = 0;
    int length_long;
    char token[128];
    int conversion_succeeded;

    if (!get_character || !unget_character || !format) {
        return -1;
    }
    while (*format_cursor != 0) {
        if (*format_cursor == '%') {
            format_cursor++;
            length_long = 0;
            if (*format_cursor == 'l') {
                length_long = 1;
                format_cursor++;
            }
            if (*format_cursor == 'd' || *format_cursor == 'i' ||
                *format_cursor == 'u' || *format_cursor == 'x' ||
                *format_cursor == 'X' || *format_cursor == 'o') {
                conversion_succeeded = scan_integer_conversion(
                    get_character, unget_character, context, *format_cursor,
                    length_long, arguments, token, sizeof(token));
                if (!conversion_succeeded) {
                    return fail_or_partial(assigned);
                }
                assigned++;
                format_cursor++;
                continue;
            }
            if (*format_cursor == 'f') {
                conversion_succeeded = scan_float_conversion(
                    get_character, unget_character, context, length_long, arguments,
                    token, sizeof(token));
                if (!conversion_succeeded) {
                    return fail_or_partial(assigned);
                }
                assigned++;
                format_cursor++;
                continue;
            }
            if (*format_cursor == 'c') {
                conversion_succeeded =
                    scan_character_conversion(get_character, context, arguments);
                if (!conversion_succeeded) {
                    return fail_or_partial(assigned);
                }
                assigned++;
                format_cursor++;
                continue;
            }
            if (*format_cursor == 's') {
                conversion_succeeded = scan_string_conversion(
                    get_character, unget_character, context, arguments);
                if (!conversion_succeeded) {
                    return fail_or_partial(assigned);
                }
                assigned++;
                format_cursor++;
                continue;
            }
            if (*format_cursor == '%') {
                if (!match_literal_percent(get_character, unget_character, context)) {
                    return assigned;
                }
                format_cursor++;
                continue;
            }
            return assigned;
        }
        if (isspace((unsigned char)*format_cursor)) {
            while (isspace((unsigned char)*format_cursor)) {
                format_cursor++;
            }
            blocks_scan_skip_spaces(get_character, unget_character, context);
            continue;
        }
        conversion_succeeded = match_literal_character(
            get_character, unget_character, context, *format_cursor);
        if (conversion_succeeded < 0) {
            return fail_or_partial(assigned);
        }
        if (conversion_succeeded == 0) {
            return assigned;
        }
        format_cursor++;
    }
    return assigned;
}
