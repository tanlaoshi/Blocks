/*
 * Board.c — Pixel 8（shiba）板包
 */
#include "Board.h"

const char *BoardName(void) {
    return BOARD_NAME;
}

int BoardDramProbe(UINT64 *Base, UINT64 *Size) {
    (void)Base;
    (void)Size;
    return -1;
}

int BoardFbSetup(BOOT_HANDOFF *Info) {
    (void)Info;
    return -1;
}
