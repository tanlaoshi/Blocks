/*
 * atexit.c — atexit / exit / abort（全称下划线）
 */
#include <stdlib.h>
#include <Blocks/Syscall.h>

#define ATEXIT_HANDLER_MAX 32

static void (*gExitHandlers[ATEXIT_HANDLER_MAX])(void);
static int gExitHandlerCount;

int atexit(void (*handler)(void)) {
    if (!handler || gExitHandlerCount >= ATEXIT_HANDLER_MAX) {
        return -1;
    }
    gExitHandlers[gExitHandlerCount++] = handler;
    return 0;
}

void abort(void) {
    blocks_exit(1);
    for (;;) {
    }
}

void exit(int status) {
    while (gExitHandlerCount > 0) {
        gExitHandlerCount--;
        gExitHandlers[gExitHandlerCount]();
    }
    blocks_exit(status);
    for (;;) {
    }
}
