/*
 * DeconTriggerEnable.c — ABL 交权后重开 DECON0 硬件 TE 触发
 *
 * 证据（omarchy husky / zuma，与 shiba 同 SoC）：
 *   ABL 离开时 TRIG_CON @0x19470030 = 0x3070（HW_TRIG_MASK_DECON 置位、无 HW_TRIG_EN）
 *   → 屏停在 Google logo，写 splash 缓冲不刷新。
 *   写 0x3061：清 mask、开 HW_TRIG_EN → 每 TE 扫一帧（对表 cal_9845 regs-decon.h）。
 * 仅写 TRIG_CON；不 init DSI / 面板。
 */
#include "Board.h"
#include "BoardConfig.h"
#include "HalSerial.h"

#ifndef BOARD_BRINGUP_SPLASH
#define BOARD_BRINGUP_SPLASH 0
#endif

#if BOARD_BRINGUP_SPLASH

#define DECON0_TRIGGER_CONTROL_PHYSICAL 0x19470030ULL
#define DECON0_TRIGGER_CONTROL_ENABLE   0x00003061u

static void WriteHex32(UINT32 Value) {
    static const char Digits[] = "0123456789abcdef";
    char Text[11];
    UINTN Index;

    Text[0] = '0';
    Text[1] = 'x';
    for (Index = 0; Index < 8u; Index++) {
        Text[2u + Index] = Digits[(Value >> (28u - Index * 4u)) & 0xFu];
    }
    Text[10] = '\0';
    HalSerialWriteShell(Text);
}

int BoardBringUpDeconTriggerEnable(void) {
    volatile UINT32 *TriggerControl =
        (volatile UINT32 *)(UINTN)DECON0_TRIGGER_CONTROL_PHYSICAL;
    UINT32 Before;
    UINT32 After;

    Before = *TriggerControl;
    HalSerialWriteShell("decon: trig ");
    WriteHex32(Before);
    HalSerialWriteShell("\r\n");

    *TriggerControl = DECON0_TRIGGER_CONTROL_ENABLE;
    __asm__ volatile("dsb sy" ::: "memory");
    After = *TriggerControl;

    HalSerialWriteShell("decon: set ");
    WriteHex32(After);
    HalSerialWriteShell("\r\n");
    return (After == DECON0_TRIGGER_CONTROL_ENABLE) ? 0 : -1;
}

#else

int BoardBringUpDeconTriggerEnable(void) {
    return -1;
}

#endif
