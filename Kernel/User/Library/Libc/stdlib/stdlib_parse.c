/*
 * stdlib_parse.c — strtol / strtoul / strtod（全称下划线）
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static int digit_value(int character, int base) {
    int value;

    if (character >= '0' && character <= '9') {
        value = character - '0';
    } else if (character >= 'a' && character <= 'z') {
        value = character - 'a' + 10;
    } else if (character >= 'A' && character <= 'Z') {
        value = character - 'A' + 10;
    } else {
        return -1;
    }
    if (value >= base) {
        return -1;
    }
    return value;
}

static unsigned long parse_unsigned(
    const char *text,
    char **end_pointer,
    int base,
    int *negative_out,
    int *converted_out
) {
    const char *cursor = text;
    unsigned long accumulator = 0;
    int negative = 0;
    int converted = 0;
    int digit;
    unsigned long cutoff;
    unsigned long cut_limit;

    if (negative_out) {
        *negative_out = 0;
    }
    if (converted_out) {
        *converted_out = 0;
    }
    if (!cursor) {
        if (end_pointer) {
            *end_pointer = (char *)text;
        }
        return 0;
    }
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '+' || *cursor == '-') {
        negative = (*cursor == '-');
        cursor++;
    }
    if (base == 0) {
        if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
            base = 16;
        } else if (cursor[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    }
    if (base == 16 && cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (end_pointer) {
            *end_pointer = (char *)text;
        }
        return 0;
    }

    cutoff = ULONG_MAX / (unsigned long)base;
    cut_limit = ULONG_MAX % (unsigned long)base;
    for (;;) {
        digit = digit_value((unsigned char)*cursor, base);
        if (digit < 0) {
            break;
        }
        if (accumulator > cutoff ||
            (accumulator == cutoff && (unsigned long)digit > cut_limit)) {
            converted = 1;
            accumulator = ULONG_MAX;
            errno = ERANGE;
            cursor++;
            while (digit_value((unsigned char)*cursor, base) >= 0) {
                cursor++;
            }
            break;
        }
        accumulator =
            accumulator * (unsigned long)base + (unsigned long)digit;
        converted = 1;
        cursor++;
    }
    if (!converted) {
        if (end_pointer) {
            *end_pointer = (char *)text;
        }
        return 0;
    }
    if (end_pointer) {
        *end_pointer = (char *)cursor;
    }
    if (negative_out) {
        *negative_out = negative;
    }
    if (converted_out) {
        *converted_out = converted;
    }
    return accumulator;
}

unsigned long strtoul(const char *text, char **end_pointer, int base) {
    int negative = 0;
    unsigned long accumulator;

    accumulator = parse_unsigned(text, end_pointer, base, &negative, 0);
    if (negative) {
        return (unsigned long)(-(long)accumulator);
    }
    return accumulator;
}

long strtol(const char *text, char **end_pointer, int base) {
    int negative = 0;
    unsigned long accumulator;
    int converted = 0;

    accumulator = parse_unsigned(text, end_pointer, base, &negative, &converted);
    if (!converted) {
        return 0;
    }
    if (negative) {
        if (accumulator > (unsigned long)LONG_MAX + 1ul) {
            errno = ERANGE;
            return LONG_MIN;
        }
        return -(long)accumulator;
    }
    if (accumulator > (unsigned long)LONG_MAX) {
        errno = ERANGE;
        return LONG_MAX;
    }
    return (long)accumulator;
}

double strtod(const char *text, char **end_pointer) {
    const char *cursor = text;
    int negative = 0;
    double accumulator = 0.0;
    double fraction = 0.0;
    double place = 0.1;
    int converted = 0;
    int exponent = 0;
    int exponent_negative = 0;
    int exponent_converted = 0;
    int step;
    double scale;

    if (!cursor) {
        if (end_pointer) {
            *end_pointer = (char *)text;
        }
        return 0.0;
    }
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '+' || *cursor == '-') {
        negative = (*cursor == '-');
        cursor++;
    }
    while (isdigit((unsigned char)*cursor)) {
        accumulator = accumulator * 10.0 + (double)(*cursor - '0');
        converted = 1;
        cursor++;
    }
    if (*cursor == '.') {
        cursor++;
        while (isdigit((unsigned char)*cursor)) {
            fraction += place * (double)(*cursor - '0');
            place *= 0.1;
            converted = 1;
            cursor++;
        }
    }
    accumulator += fraction;
    if (*cursor == 'e' || *cursor == 'E') {
        const char *exponent_mark = cursor;

        cursor++;
        if (*cursor == '+' || *cursor == '-') {
            exponent_negative = (*cursor == '-');
            cursor++;
        }
        while (isdigit((unsigned char)*cursor)) {
            exponent = exponent * 10 + (*cursor - '0');
            exponent_converted = 1;
            cursor++;
        }
        if (!exponent_converted) {
            cursor = exponent_mark;
        } else if (exponent_negative) {
            exponent = -exponent;
        }
    }
    if (!converted) {
        if (end_pointer) {
            *end_pointer = (char *)text;
        }
        return 0.0;
    }
    scale = 1.0;
    if (exponent > 0) {
        for (step = 0; step < exponent; step++) {
            scale *= 10.0;
        }
        accumulator *= scale;
    } else if (exponent < 0) {
        for (step = 0; step < -exponent; step++) {
            scale *= 10.0;
        }
        accumulator /= scale;
    }
    if (end_pointer) {
        *end_pointer = (char *)cursor;
    }
    return negative ? -accumulator : accumulator;
}
