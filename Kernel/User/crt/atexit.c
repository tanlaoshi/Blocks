/*
 * atexit.c — atexit / abort（libc-atexit）
 */
#include <stdlib.h>
#include <Blocks/syscall.h>

#define ATEXIT_MAX 32

static void (*gHandlers[ATEXIT_MAX])(void);
static int gCount;

int atexit(void (*func)(void)) {
    if (!func || gCount >= ATEXIT_MAX) {
        return -1;
    }
    gHandlers[gCount++] = func;
    return 0;
}

void abort(void) {
    blocks_exit(1);
    for (;;) {
    }
}

void exit(int status) {
    while (gCount > 0) {
        gCount--;
        gHandlers[gCount]();
    }
    blocks_exit(status);
    for (;;) {
    }
}
