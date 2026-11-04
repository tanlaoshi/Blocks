/*
 * stdlib_search.c — bsearch / srand / rand（全称下划线；C99）
 */
#include <stdlib.h>

void *bsearch(
    const void *key,
    const void *base,
    size_t element_count,
    size_t element_size,
    int (*compare)(const void *, const void *)
) {
    size_t low = 0;
    size_t high = element_count;
    const unsigned char *bytes = (const unsigned char *)base;

    if (!key || !base || !compare || element_size == 0) {
        return 0;
    }
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        const void *element = bytes + middle * element_size;
        int order = compare(key, element);

        if (order < 0) {
            high = middle;
        } else if (order > 0) {
            low = middle + 1;
        } else {
            return (void *)element;
        }
    }
    return 0;
}

static unsigned long gRandomState = 1ul;

void srand(unsigned int seed) {
    gRandomState = (unsigned long)seed;
}

int rand(void) {
    /* Park–Miller 型；结果落入 0..32767 */
    gRandomState = gRandomState * 1103515245ul + 12345ul;
    return (int)((gRandomState / 65536ul) % 32768ul);
}
