/*
 * atexit.c — atexit / exit / abort（展览级命名）
 */
#include <stdlib.h>
#include <Blocks/syscall.h>

#define ATEXIT_HANDLER_MAX 32

static void (*gExitHandlers[ATEXIT_HANDLER_MAX])(void);
static int gExitHandlerCount;

int atexit(void (*Handler)(void)) {
    if (!Handler || gExitHandlerCount >= ATEXIT_HANDLER_MAX) {
        return -1;
    }
    gExitHandlers[gExitHandlerCount++] = Handler;
    return 0;
}

void abort(void) {
    blocks_exit(1);
    for (;;) {
    }
}

void exit(int Status) {
    while (gExitHandlerCount > 0) {
        gExitHandlerCount--;
        gExitHandlers[gExitHandlerCount]();
    }
    blocks_exit(Status);
    for (;;) {
    }
}
