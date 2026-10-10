/*
 * Board.c — QEMU riscv64 virt 板包
 *
 * 【初学者】
 * - Hal/RiscV：同 Arm64 角色；BoardConfig.h 定 UART/RAM。
 * - 入口：BoardName。
 */
#include "Board.h"

const char *BoardName(void) {
    return BOARD_NAME;
}
