/*
 * stdlib.c — abs / labs / atoi / atol / qsort（全称下划线）
 * malloc → malloc.c；exit → atexit.c；bsearch/rand → stdlib_search.c
 */
#include <ctype.h>
#include <stdlib.h>

int abs(int value) {
    return value < 0 ? -value : value;
}

long labs(long value) {
    return value < 0 ? -value : value;
}

int atoi(const char *string) {
    return (int)atol(string);
}

long atol(const char *string) {
    long sign = 1;
    long value = 0;

    if (!string) {
        return 0;
    }
    while (isspace((unsigned char)*string)) {
        string++;
    }
    if (*string == '+' || *string == '-') {
        if (*string == '-') {
            sign = -1;
        }
        string++;
    }
    while (isdigit((unsigned char)*string)) {
        value = value * 10 + (*string - '0');
        string++;
    }
    return sign * value;
}

static void swap_bytes(unsigned char *left, unsigned char *right, size_t byte_count) {
    size_t index;

    for (index = 0; index < byte_count; index++) {
        unsigned char temporary = left[index];

        left[index] = right[index];
        right[index] = temporary;
    }
}

void qsort(void *base, size_t element_count, size_t element_size,
           int (*compare)(const void *, const void *)) {
    unsigned char *bytes;
    size_t outer;
    size_t inner;

    if (!base || !compare || element_count < 2 || element_size == 0) {
        return;
    }
    bytes = (unsigned char *)base;
    /* 教学用插入排序：稳定、无递归栈 */
    for (outer = 1; outer < element_count; outer++) {
        for (inner = outer; inner > 0; inner--) {
            unsigned char *left = bytes + (inner - 1) * element_size;
            unsigned char *right = bytes + inner * element_size;

            if (compare(left, right) <= 0) {
                break;
            }
            swap_bytes(left, right, element_size);
        }
    }
}
