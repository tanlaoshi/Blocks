/*
 * Board.c — MIX 2（chiron）板包
 */
#include "Board.h"

const char *BoardName(void) {
    return BOARD_NAME;
}

/* Pixel DECON 专用；Handoff 共用，MIX2 空实现 */
int BoardBringUpDeconTriggerEnable(void) {
    return 0;
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
