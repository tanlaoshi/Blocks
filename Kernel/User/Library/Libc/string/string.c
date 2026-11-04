/*
 * string.c — 核心 C 字符串 / 内存块（全称下划线；函数名仍为 ISO）
 */
#include <stdlib.h>
#include <string.h>

size_t strlen(const char *string) {
    size_t length = 0;

    if (!string) {
        return 0;
    }
    while (string[length] != 0) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    if (!left) {
        left = "";
    }
    if (!right) {
        right = "";
    }
    while (*left != 0 && *left == *right) {
        left++;
        right++;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncmp(const char *left, const char *right, size_t max_length) {
    size_t index;

    if (!left) {
        left = "";
    }
    if (!right) {
        right = "";
    }
    for (index = 0; index < max_length; index++) {
        unsigned char left_byte = (unsigned char)left[index];
        unsigned char right_byte = (unsigned char)right[index];

        if (left_byte != right_byte || left_byte == 0) {
            return left_byte - right_byte;
        }
    }
    return 0;
}

char *strcpy(char *destination, const char *source) {
    char *cursor;

    if (!destination) {
        return destination;
    }
    cursor = destination;
    if (!source) {
        *cursor = 0;
        return destination;
    }
    while ((*cursor++ = *source++) != 0) {
    }
    return destination;
}

char *strncpy(char *destination, const char *source, size_t max_length) {
    size_t index;

    if (!destination) {
        return destination;
    }
    if (!source) {
        source = "";
    }
    for (index = 0; index < max_length && source[index] != 0; index++) {
        destination[index] = source[index];
    }
    for (; index < max_length; index++) {
        destination[index] = 0;
    }
    return destination;
}

char *strcat(char *destination, const char *source) {
    char *cursor;

    if (!destination) {
        return destination;
    }
    cursor = destination;
    while (*cursor != 0) {
        cursor++;
    }
    if (!source) {
        *cursor = 0;
        return destination;
    }
    while ((*cursor++ = *source++) != 0) {
    }
    return destination;
}

char *strdup(const char *string) {
    size_t byte_count;
    char *copy;

    if (!string) {
        return 0;
    }
    byte_count = strlen(string) + 1;
    copy = (char *)malloc(byte_count);
    if (!copy) {
        return 0;
    }
    memcpy(copy, string, byte_count);
    return copy;
}

char *strchr(const char *string, int character) {
    unsigned char wanted = (unsigned char)character;

    if (!string) {
        return 0;
    }
    for (;;) {
        if ((unsigned char)*string == wanted) {
            return (char *)string;
        }
        if (*string == 0) {
            return 0;
        }
        string++;
    }
}

char *strstr(const char *haystack, const char *needle) {
    size_t needle_length;
    size_t index;

    if (!haystack) {
        return 0;
    }
    if (!needle || needle[0] == 0) {
        return (char *)haystack;
    }
    needle_length = strlen(needle);
    for (index = 0; haystack[index] != 0; index++) {
        if (strncmp(haystack + index, needle, needle_length) == 0) {
            return (char *)(haystack + index);
        }
    }
    return 0;
}

void *memcpy(void *destination, const void *source, size_t byte_count) {
    unsigned char *destination_bytes = (unsigned char *)destination;
    const unsigned char *source_bytes = (const unsigned char *)source;
    size_t index;

    for (index = 0; index < byte_count; index++) {
        destination_bytes[index] = source_bytes[index];
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t byte_count) {
    unsigned char *destination_bytes = (unsigned char *)destination;
    const unsigned char *source_bytes = (const unsigned char *)source;
    size_t index;

    if (destination_bytes == source_bytes || byte_count == 0) {
        return destination;
    }
    if (destination_bytes < source_bytes) {
        for (index = 0; index < byte_count; index++) {
            destination_bytes[index] = source_bytes[index];
        }
    } else {
        for (index = byte_count; index > 0; index--) {
            destination_bytes[index - 1] = source_bytes[index - 1];
        }
    }
    return destination;
}

void *memset(void *destination, int byte_value, size_t byte_count) {
    unsigned char *destination_bytes = (unsigned char *)destination;
    size_t index;

    for (index = 0; index < byte_count; index++) {
        destination_bytes[index] = (unsigned char)byte_value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t byte_count) {
    const unsigned char *left_bytes = (const unsigned char *)left;
    const unsigned char *right_bytes = (const unsigned char *)right;
    size_t index;

    for (index = 0; index < byte_count; index++) {
        if (left_bytes[index] != right_bytes[index]) {
            return left_bytes[index] - right_bytes[index];
        }
    }
    return 0;
}
