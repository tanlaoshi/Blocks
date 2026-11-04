/*
 * printf_float.c — 教学用 %f / %e / %g（全称下划线）
 */
#include <float.h>
#include <stddef.h>

static void put_character(char *destination, size_t capacity, size_t *position,
                         char character) {
    if (destination && capacity > 0 && *position + 1 < capacity) {
        destination[*position] = character;
    }
    (*position)++;
}

static void put_string(char *destination, size_t capacity, size_t *position,
                      const char *string) {
    while (*string != 0) {
        put_character(destination, capacity, position, *string++);
    }
}

static void put_unsigned_long(char *destination, size_t capacity, size_t *position,
                            unsigned long value) {
    char digits[32];
    int digit_count = 0;

    if (value == 0) {
        put_character(destination, capacity, position, '0');
        return;
    }
    while (value > 0 && digit_count < (int)sizeof(digits)) {
        digits[digit_count++] = (char)('0' + (value % 10ul));
        value /= 10ul;
    }
    while (digit_count > 0) {
        put_character(destination, capacity, position, digits[--digit_count]);
    }
}

static int is_not_a_number(double value) {
    return value != value;
}

static int is_infinity(double value) {
    return (value > DBL_MAX) || (value < -DBL_MAX);
}

static double absolute_value(double value) {
    return value < 0.0 ? -value : value;
}

static double power_of_ten(int exponent) {
    double result = 1.0;
    int index;

    if (exponent >= 0) {
        for (index = 0; index < exponent; index++) {
            result *= 10.0;
        }
    } else {
        for (index = 0; index < -exponent; index++) {
            result /= 10.0;
        }
    }
    return result;
}

/* 写入定点数（无指数）；返回已逻辑写入长度 */
static size_t format_fixed(char *destination, size_t capacity, double value,
                          int precision, int *out_exponent) {
    size_t position = 0;
    double absolute;
    unsigned long integer_part;
    double fraction;
    int index;

    if (out_exponent) {
        *out_exponent = 0;
    }
    if (value < 0.0) {
        put_character(destination, capacity, &position, '-');
        value = -value;
    }
    absolute = value;
    integer_part = (unsigned long)absolute;
    fraction = absolute - (double)integer_part;
    put_unsigned_long(destination, capacity, &position, integer_part);
    if (precision > 0) {
        unsigned long fraction_part;
        char fraction_digits[32];
        int digit_count = precision;

        put_character(destination, capacity, &position, '.');
        fraction *= power_of_ten(precision);
        fraction += 0.5; /* 简单四舍五入 */
        fraction_part = (unsigned long)fraction;
        if (digit_count > (int)sizeof(fraction_digits)) {
            digit_count = (int)sizeof(fraction_digits);
        }
        for (index = digit_count - 1; index >= 0; index--) {
            fraction_digits[index] = (char)('0' + (fraction_part % 10ul));
            fraction_part /= 10ul;
        }
        for (index = 0; index < digit_count; index++) {
            put_character(destination, capacity, &position, fraction_digits[index]);
        }
    }
    return position;
}

static size_t format_scientific(char *destination, size_t capacity, double value,
                               int precision) {
    size_t position = 0;
    int exponent = 0;
    double mantissa;
    char mantissa_buffer[64];
    size_t mantissa_length;
    size_t index;

    if (value < 0.0) {
        put_character(destination, capacity, &position, '-');
        value = -value;
    }
    if (value == 0.0) {
        put_character(destination, capacity, &position, '0');
        if (precision > 0) {
            put_character(destination, capacity, &position, '.');
            while (precision-- > 0) {
                put_character(destination, capacity, &position, '0');
            }
        }
        put_string(destination, capacity, &position, "e+00");
        return position;
    }
    mantissa = value;
    while (mantissa >= 10.0) {
        mantissa /= 10.0;
        exponent++;
    }
    while (mantissa < 1.0) {
        mantissa *= 10.0;
        exponent--;
    }
    mantissa_length =
        format_fixed(mantissa_buffer, sizeof(mantissa_buffer), mantissa, precision,
                    0);
    for (index = 0; index < mantissa_length; index++) {
        put_character(destination, capacity, &position, mantissa_buffer[index]);
    }
    put_character(destination, capacity, &position, 'e');
    if (exponent < 0) {
        put_character(destination, capacity, &position, '-');
        exponent = -exponent;
    } else {
        put_character(destination, capacity, &position, '+');
    }
    if (exponent < 10) {
        put_character(destination, capacity, &position, '0');
    }
    put_unsigned_long(destination, capacity, &position, (unsigned long)exponent);
    return position;
}

/*
 * Specifier: 'f'/'F'/'e'/'E'/'g'/'G'
 * Precision: <0 → 默认 6
 * 返回写入长度（不含 NUL）；Destination 可 NULL 仅计量
 */
size_t printf_format_double(char *destination, size_t capacity, double value,
                          int specifier, int precision) {
    size_t position = 0;
    int lowercase =
        (specifier == 'f' || specifier == 'e' || specifier == 'g');

    if (precision < 0) {
        precision = 6;
    }
    if (is_not_a_number(value)) {
        put_string(destination, capacity, &position, lowercase ? "nan" : "NAN");
    } else if (is_infinity(value)) {
        if (value < 0.0) {
            put_character(destination, capacity, &position, '-');
        }
        put_string(destination, capacity, &position, lowercase ? "inf" : "INF");
    } else if (specifier == 'f' || specifier == 'F') {
        position = format_fixed(destination, capacity, value, precision, 0);
    } else if (specifier == 'e' || specifier == 'E') {
        position = format_scientific(destination, capacity, value, precision);
        if (!lowercase && destination && capacity > 0) {
            size_t index;

            for (index = 0; index < position && index + 1 < capacity; index++) {
                if (destination[index] == 'e') {
                    destination[index] = 'E';
                }
            }
        }
    } else {
        /* %g：|Value| 很大或很小时用 e，否则 f；去尾零简化为固定精度 f */
        double absolute = absolute_value(value);

        if (absolute != 0.0 &&
            (absolute < 1e-4 || absolute >= power_of_ten(precision))) {
            position = format_scientific(destination, capacity, value,
                                        precision > 0 ? precision - 1 : 0);
        } else {
            position = format_fixed(destination, capacity, value, precision, 0);
        }
    }
    if (destination && capacity > 0) {
        size_t end = (position < capacity) ? position : capacity - 1;

        destination[end] = 0;
    }
    return position;
}
