/*
 * stdio_scan_token.c — scan 引擎：空白跳过与整数/浮点词法收集
 */
#include <ctype.h>
#include <stdio.h>

#include "stdio_private.h"

int blocks_scan_peek_character(blocks_scan_get_character get_character,
                            blocks_scan_unget_character unget_character,
                            void *context) {
    int character = get_character(context);

    if (character != EOF) {
        unget_character(context, character);
    }
    return character;
}

void blocks_scan_skip_spaces(blocks_scan_get_character get_character,
                          blocks_scan_unget_character unget_character,
                          void *context) {
    int character;

    for (;;) {
        character = get_character(context);
        if (character == EOF) {
            return;
        }
        if (!isspace((unsigned char)character)) {
            unget_character(context, character);
            return;
        }
    }
}

static int digit_value_for_base(int character, int base) {
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

int blocks_scan_collect_integer_token(blocks_scan_get_character get_character,
                                  blocks_scan_unget_character unget_character,
                                  void *context, char *buffer, size_t capacity,
                                  int base) {
    size_t length = 0;
    int character;
    int seen_digit = 0;
    int collect_base = base;

    if (capacity == 0 || base < 0 || base == 1 || base > 36) {
        return 0;
    }
    character = get_character(context);
    if (character == '+' || character == '-') {
        buffer[length++] = (char)character;
        character = get_character(context);
    }
    if (base == 0) {
        if (character == '0') {
            buffer[length++] = (char)character;
            character = blocks_scan_peek_character(get_character, unget_character,
                                               context);
            if (character == 'x' || character == 'X') {
                character = get_character(context);
                buffer[length++] = (char)character;
                collect_base = 16;
                character = get_character(context);
            } else {
                collect_base = 8;
                character = get_character(context);
                seen_digit = 1;
            }
        } else {
            collect_base = 10;
        }
    } else if (base == 16 && character == '0') {
        buffer[length++] = (char)character;
        character = blocks_scan_peek_character(get_character, unget_character, context);
        if (character == 'x' || character == 'X') {
            character = get_character(context);
            buffer[length++] = (char)character;
            character = get_character(context);
        } else {
            seen_digit = 1;
            character = get_character(context);
        }
    }
    while (character != EOF && length < capacity - 1) {
        if (digit_value_for_base(character, collect_base) < 0) {
            break;
        }
        buffer[length++] = (char)character;
        seen_digit = 1;
        character = get_character(context);
    }
    if (character != EOF) {
        unget_character(context, character);
    }
    buffer[length] = 0;
    return seen_digit;
}

int blocks_scan_collect_float_token(blocks_scan_get_character get_character,
                                blocks_scan_unget_character unget_character,
                                void *context, char *buffer, size_t capacity) {
    size_t length = 0;
    int character;
    int seen_digit = 0;
    int seen_dot = 0;

    if (capacity == 0) {
        return 0;
    }
    character = get_character(context);
    if (character == '+' || character == '-') {
        buffer[length++] = (char)character;
        character = get_character(context);
    }
    while (character != EOF && length < capacity - 1) {
        if (isdigit((unsigned char)character)) {
            buffer[length++] = (char)character;
            seen_digit = 1;
            character = get_character(context);
            continue;
        }
        if (character == '.' && !seen_dot) {
            buffer[length++] = (char)character;
            seen_dot = 1;
            character = get_character(context);
            continue;
        }
        break;
    }
    if ((character == 'e' || character == 'E') && seen_digit &&
        length < capacity - 1) {
        buffer[length++] = (char)character;
        character = get_character(context);
        if ((character == '+' || character == '-') && length < capacity - 1) {
            buffer[length++] = (char)character;
            character = get_character(context);
        }
        while (isdigit((unsigned char)character) && length < capacity - 1) {
            buffer[length++] = (char)character;
            character = get_character(context);
        }
    }
    if (character != EOF) {
        unget_character(context, character);
    }
    buffer[length] = 0;
    return seen_digit;
}
