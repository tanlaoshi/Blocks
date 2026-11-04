/*
 * printf_format.c — PrintfFormat：整数 / 长度修饰 / 浮点挂接（展览级铺平）
 *
 * LengthKind：0 默认 / 1 = l / 2 = ll / 3 = z
 */
#include <stdarg.h>
#include <stdint.h>
#include <Blocks/syscall.h>
#include "printf_private.h"

size_t printf_format_double(char *destination, size_t capacity, double value,
                          int specifier, int precision);

enum {
    length_default = 0,
    length_long = 1,
    length_long_long = 2,
    length_size = 3
};

void printf_out_char(fmt_out *output, char character) {
    if (output->write_to_descriptor) {
        blocks_write(1, &character, 1);
        output->position++;
        return;
    }
    if (output->buffer && output->capacity > 0 &&
        output->position + 1 < output->capacity) {
        output->buffer[output->position] = character;
    }
    output->position++;
}

void printf_out_str(fmt_out *output, const char *string) {
    if (!string) {
        string = "(null)";
    }
    while (*string != 0) {
        printf_out_char(output, *string++);
    }
}

static void emit_unsigned_long_long(fmt_out *output, unsigned long long value,
                                 unsigned base, int uppercase, int zero_pad,
                                 int field_width) {
    char digits[64];
    int digit_count = 0;
    const char *alphabet =
        uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned long long remaining = value;
    int pad;

    if (base < 2 || base > 16) {
        return;
    }
    if (remaining == 0) {
        digits[digit_count++] = '0';
    } else {
        while (remaining > 0 && digit_count < (int)sizeof(digits)) {
            digits[digit_count++] = alphabet[remaining % base];
            remaining /= base;
        }
    }
    if (zero_pad && field_width > digit_count) {
        for (pad = 0; pad < field_width - digit_count; pad++) {
            printf_out_char(output, '0');
        }
    }
    while (digit_count > 0) {
        printf_out_char(output, digits[--digit_count]);
    }
}

static void emit_signed_long(fmt_out *output, long value) {
    unsigned long magnitude;

    if (value < 0) {
        printf_out_char(output, '-');
        magnitude = -(unsigned long)value;
    } else {
        magnitude = (unsigned long)value;
    }
    emit_unsigned_long_long(output, magnitude, 10, 0, 0, 0);
}

static void emit_signed_long_long(fmt_out *output, long long value) {
    unsigned long long magnitude;

    if (value < 0) {
        printf_out_char(output, '-');
        magnitude = -(unsigned long long)value;
    } else {
        magnitude = (unsigned long long)value;
    }
    emit_unsigned_long_long(output, magnitude, 10, 0, 0, 0);
}

static unsigned long long pull_unsigned(va_list arguments, int length_kind) {
    if (length_kind == length_long_long) {
        return va_arg(arguments, unsigned long long);
    }
    if (length_kind == length_long || length_kind == length_size) {
        return va_arg(arguments, unsigned long);
    }
    return va_arg(arguments, unsigned);
}

static void emit_conversion(fmt_out *output, char specifier, int length_kind,
                           int zero_pad, int field_width, int precision,
                           va_list arguments) {
    char float_buffer[96];
    double float_value;
    void *pointer;
    unsigned long long unsigned_value;
    unsigned base;

    if (specifier == '%') {
        printf_out_char(output, '%');
        return;
    }
    if (specifier == 'c') {
        printf_out_char(output, (char)va_arg(arguments, int));
        return;
    }
    if (specifier == 's') {
        printf_out_str(output, va_arg(arguments, const char *));
        return;
    }
    if (specifier == 'd' || specifier == 'i') {
        if (length_kind == length_long_long) {
            emit_signed_long_long(output, va_arg(arguments, long long));
        } else if (length_kind == length_long || length_kind == length_size) {
            emit_signed_long(output, va_arg(arguments, long));
        } else {
            emit_signed_long(output, (long)va_arg(arguments, int));
        }
        return;
    }
    if (specifier == 'u' || specifier == 'o' || specifier == 'x' ||
        specifier == 'X') {
        base = 10;
        if (specifier == 'o') {
            base = 8;
        } else if (specifier == 'x' || specifier == 'X') {
            base = 16;
        }
        unsigned_value = pull_unsigned(arguments, length_kind);
        emit_unsigned_long_long(output, unsigned_value, base, specifier == 'X',
                             zero_pad, field_width);
        return;
    }
    if (specifier == 'f' || specifier == 'F' || specifier == 'e' ||
        specifier == 'E' || specifier == 'g' || specifier == 'G') {
        float_value = va_arg(arguments, double);
        printf_format_double(float_buffer, sizeof(float_buffer), float_value,
                           specifier, precision);
        printf_out_str(output, float_buffer);
        return;
    }
    if (specifier == 'p') {
        pointer = va_arg(arguments, void *);
        printf_out_str(output, "0x");
        emit_unsigned_long_long(output, (unsigned long)(uintptr_t)pointer, 16, 0,
                             0, 0);
        return;
    }
    printf_out_char(output, '%');
    printf_out_char(output, specifier);
}

int printf_format(fmt_out *output, const char *format, va_list arguments) {
    const char *cursor;

    if (!format) {
        return 0;
    }
    for (cursor = format; *cursor != 0; cursor++) {
        int zero_pad;
        int field_width;
        int precision;
        int length_kind;

        if (*cursor != '%') {
            printf_out_char(output, *cursor);
            continue;
        }
        cursor++;
        if (*cursor == 0) {
            break;
        }
        zero_pad = 0;
        field_width = 0;
        precision = -1;
        length_kind = length_default;
        if (*cursor == '0') {
            zero_pad = 1;
            cursor++;
        }
        while (*cursor >= '0' && *cursor <= '9') {
            field_width = field_width * 10 + (*cursor - '0');
            cursor++;
        }
        if (*cursor == '.') {
            precision = 0;
            cursor++;
            while (*cursor >= '0' && *cursor <= '9') {
                precision = precision * 10 + (*cursor - '0');
                cursor++;
            }
        }
        if (*cursor == 'l') {
            cursor++;
            if (*cursor == 'l') {
                length_kind = length_long_long;
                cursor++;
            } else {
                length_kind = length_long;
            }
        } else if (*cursor == 'z') {
            length_kind = length_size;
            cursor++;
        }
        if (*cursor == 0) {
            break;
        }
        emit_conversion(output, *cursor, length_kind, zero_pad, field_width,
                       precision, arguments);
    }
    return (int)output->position;
}
