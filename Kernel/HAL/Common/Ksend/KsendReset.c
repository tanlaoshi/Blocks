/*
 * KsendReset.c — 传输成功后复位
 *
 * 板差在 HalCpuReboot（Duo → BoardCpuReboot RTC；Pi → PM watchdog；X64 → CF9/ACPI）。
 */
#include "Ksend/Private.h"
#include "Hal.h"

void KsendReboot(void) {
    KsendSerialDrainTransmitter();
    HalCpuReboot();
    KsendNote("reset not accepted; power-cycle the board");
    for (;;) {
        KsendDelayMs(1000u);
    }
}
