/*
 * stdlib_search.c — bsearch / srand / rand（展览级命名；C99）
 */
#include <stdlib.h>

void *bsearch(
    const void *Key,
    const void *Base,
    size_t ElementCount,
    size_t ElementSize,
    int (*Compare)(const void *, const void *)
) {
    size_t Low = 0;
    size_t High = ElementCount;
    const unsigned char *Bytes = (const unsigned char *)Base;

    if (!Key || !Base || !Compare || ElementSize == 0) {
        return 0;
    }
    while (Low < High) {
        size_t Middle = Low + (High - Low) / 2;
        const void *Element = Bytes + Middle * ElementSize;
        int Order = Compare(Key, Element);

        if (Order < 0) {
            High = Middle;
        } else if (Order > 0) {
            Low = Middle + 1;
        } else {
            return (void *)Element;
        }
    }
    return 0;
}

static unsigned long gRandomState = 1ul;

void srand(unsigned int Seed) {
    gRandomState = (unsigned long)Seed;
}

int rand(void) {
    /* Park–Miller 型；结果落入 0..32767 */
    gRandomState = gRandomState * 1103515245ul + 12345ul;
    return (int)((gRandomState / 65536ul) % 32768ul);
}
