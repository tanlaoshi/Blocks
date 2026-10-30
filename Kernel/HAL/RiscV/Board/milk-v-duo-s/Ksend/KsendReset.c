/*
 * KsendReset.c — 传输成功后复位（SG2000 温复位）
 *
 * 依据：本 SoC 厂商 U-Boot 自己的 reset_cpu 就走这条路（RISC-V S-mode 可写）：
 *   Source/DUO/sdk/duo-buildroot-sdk-v2-2.0.1/u-boot-2021.10/board/cvitek/
 *     cv181x/board.c:307-317   cv_system_reset()
 *     cv181x/cv181x_reg.h:140/147-149/153/157   寄存器号
 * 步骤：置 RTC_EN_WARM_RST_REQ → 读回确认 → 写 UNLOCKKEY → RTC_CTRL0 置 bit4。
 *
 * 为什么不用 HalCpuReboot()：RISC-V 上它只是 HalCpuPark()（停住，等人拔电），
 * 而这里要的是无人值守的循环，必须真复位。
 */
#include "Ksend/Private.h"
#include "Hal.h"

/* cv181x_reg.h:140 RTC_SYS_BASE + :147/:153 两个基址 */
#define KSEND_RTC_SYSTEM_BASE     0x05000000ULL
#define KSEND_RTC_CONTROL_BASE    (KSEND_RTC_SYSTEM_BASE + 0x00025000ULL)
#define KSEND_RTC_REQUEST_BASE    (KSEND_RTC_SYSTEM_BASE + 0x00026000ULL)
/* cv181x_reg.h:148/149/157 的寄存器偏移 */
#define KSEND_RTC_CONTROL_UNLOCK  0x4u
#define KSEND_RTC_CONTROL         0x8u
#define KSEND_RTC_WARM_RESET      0xCCu
/* board.c:312-313 的两个常量 */
#define KSEND_RTC_UNLOCK_KEY      0xAB18u
#define KSEND_RTC_CONTROL_KEEP    0xFFFF0800u
#define KSEND_RTC_WARM_RESET_BIT  (1u << 4)
/* 读回等待上限：正常情况写下去就复位，这两条只是兜底 */
#define KSEND_RTC_WAIT_MS         100u
#define KSEND_RTC_PARK_MS         1000u

static volatile UINT32 *KsendRegister32(UINT64 Address) {
    return (volatile UINT32 *)(UINTN)Address;
}

static void KsendRtcWarmReset(void) {
    volatile UINT32 *Request =
        KsendRegister32(KSEND_RTC_REQUEST_BASE + KSEND_RTC_WARM_RESET);
    volatile UINT32 *Unlock =
        KsendRegister32(KSEND_RTC_CONTROL_BASE + KSEND_RTC_CONTROL_UNLOCK);
    volatile UINT32 *Control =
        KsendRegister32(KSEND_RTC_CONTROL_BASE + KSEND_RTC_CONTROL);
    UINT32 Start;

    /* 与厂商同序：先发请求并读回，再解锁 + 置复位位 */
    *Request = 1u;
    Start = KsendMilliseconds();
    while (*Request != 1u && (UINT32)(KsendMilliseconds() - Start) < KSEND_RTC_WAIT_MS) {
    }
    *Unlock = KSEND_RTC_UNLOCK_KEY;
    *Control = *Control | (KSEND_RTC_CONTROL_KEEP | KSEND_RTC_WARM_RESET_BIT);
}

void KsendReboot(void) {
    KsendRtcWarmReset();
    /* 复位没被接受（地址/权限不对）：别静默停住，留一句给串口 */
    KsendNote("reset not accepted by RTC; power-cycle the board");
    for (;;) {
        KsendDelayMs(KSEND_RTC_PARK_MS);
    }
}
