/*
 * BoardReboot.c — Duo S 真复位（SG2000 RTC 温复位）
 *
 * 从原 Ksend/KsendReset.c 迁出：Common Ksend 只调 HalCpuReboot → 本弱符号。
 * 依据：厂商 U-Boot cv_system_reset（cv181x/board.c）。
 */
#include "BoardConfig.h"
#include "BootTypes.h"

#define BOARD_RTC_SYSTEM_BASE    0x05000000ULL
#define BOARD_RTC_CONTROL_BASE   (BOARD_RTC_SYSTEM_BASE + 0x00025000ULL)
#define BOARD_RTC_REQUEST_BASE   (BOARD_RTC_SYSTEM_BASE + 0x00026000ULL)
#define BOARD_RTC_CONTROL_UNLOCK 0x4u
#define BOARD_RTC_CONTROL        0x8u
#define BOARD_RTC_WARM_RESET     0xCCu
#define BOARD_RTC_UNLOCK_KEY     0xAB18u
#define BOARD_RTC_CONTROL_KEEP   0xFFFF0800u
#define BOARD_RTC_WARM_RESET_BIT (1u << 4)

static volatile UINT32 *BoardRegister32(UINT64 Address) {
    return (volatile UINT32 *)(UINTN)Address;
}

void BoardCpuReboot(void) {
    volatile UINT32 *Request =
        BoardRegister32(BOARD_RTC_REQUEST_BASE + BOARD_RTC_WARM_RESET);
    volatile UINT32 *Unlock =
        BoardRegister32(BOARD_RTC_CONTROL_BASE + BOARD_RTC_CONTROL_UNLOCK);
    volatile UINT32 *Control =
        BoardRegister32(BOARD_RTC_CONTROL_BASE + BOARD_RTC_CONTROL);

    *Request = 1u;
    *Unlock = BOARD_RTC_UNLOCK_KEY;
    *Control = *Control | (BOARD_RTC_CONTROL_KEEP | BOARD_RTC_WARM_RESET_BIT);
}
