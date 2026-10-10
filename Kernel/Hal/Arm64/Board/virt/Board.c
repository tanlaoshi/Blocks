/*
 * Board.c — QEMU aarch64 virt 板包
 *
 * 【初学者】
 * - Hal/Arm64：板名与常量；链接脚本/Handoff 读 BoardConfig.h。
 * - 入口：BoardName。
 */
#include "Board.h"

const char *BoardName(void) {
    return BOARD_NAME;
}
