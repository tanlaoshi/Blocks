/*
 * stdlib_search.c — bsearch / qsort 已在 stdlib.c；本文件 bsearch + rand（C99）
 */
#include <stdlib.h>

void *bsearch(
    const void *key,
    const void *base,
    size_t nmemb,
    size_t size,
    int (*compar)(const void *, const void *)
) {
    size_t Low = 0;
    size_t High = nmemb;
    const unsigned char *Bytes = (const unsigned char *)base;

    if (!key || !base || !compar || size == 0) {
        return 0;
    }
    while (Low < High) {
        size_t Mid = Low + (High - Low) / 2;
        const void *Element = Bytes + Mid * size;
        int Cmp = compar(key, Element);

        if (Cmp < 0) {
            High = Mid;
        } else if (Cmp > 0) {
            Low = Mid + 1;
        } else {
            return (void *)Element;
        }
    }
    return 0;
}

static unsigned long gRandState = 1ul;

void srand(unsigned int seed) {
    gRandState = (unsigned long)seed;
}

int rand(void) {
    /* 数值配方：Park–Miller 型；结果落入 0..32767 */
    gRandState = gRandState * 1103515245ul + 12345ul;
    return (int)((gRandState / 65536ul) % 32768ul);
}
