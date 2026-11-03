/*
 * CardWatch.c — Pi µSD 拔卡盯梢（对表 X64 usb-boot-unplug）
 *
 * Worker→HalUsbMscPollUnplug→本函数：读探失败或 I/O 已标 CardLost →
 * 钉 BootMediumGone → Clock/Notify 画大窗关机。有 I/O 忙则本拍跳过。
 *
 * 勿只信 CMD13：手测无卡仍可伪 R1 成功；开 Settings 触盘才认拔。
 * 闲置探活与触盘同形：CMD17 读 LBA0。
 *
 * 串口：ShellOwn 后须 HalSerialWriteShell（BootMark 不上 UART）。
 */
#include "Sdhost/Private.h"
#include "FileSystem.h"
#include "HalSerial.h"

static int SdhostProbeCardPresent(void) {
    UINT8 Probe[512];

    /* 先 CMD13：真失败可快路径；伪成功再读一块钉死 */
    if (SdhostCommandSend(13, gSdhostRelativeCardAddress, RESPONSE_R1, 0, 0) !=
        0) {
        return 0;
    }
    if (SdhostReadOneBlock(0, Probe) != 0) {
        return 0;
    }
    return 1;
}

static void SdhostAnnounceUnplug(void) {
    HalSerialWriteShell("\nFS: sdhost unplug (boot medium removed)\n");
    FileSystemNotifyBootWatchUnplug();
}

/*
 * 1 = 本拍已确认 µSD 失根（已 Notify + Ready=0）。
 * 节流 ~0.5s；探活失败或 CardLost 即认拔。
 */
int BoardSdhostPollUnplug(void) {
    static UINT64 LastCheckTick;
    UINT64 Now;
    UINT32 TicksPerSecond;

    if (!gSdhostReady) {
        /* I/O 路径已 Ready=0+Notify：仍回报 1，催 Clock 画窗 */
        if (gSdhostCardLost || FileSystemBootMediumGone()) {
            return 1;
        }
        return 0;
    }

    Now = HalCpuTicks(0);
    TicksPerSecond = HalTicksPerSec();
    if (TicksPerSecond == 0) {
        TicksPerSecond = 250;
    }
    if (LastCheckTick != 0 &&
        (Now - LastCheckTick) < ((UINT64)TicksPerSecond / 2u)) {
        return 0;
    }
    LastCheckTick = Now;

    if (gSdhostIoBusy) {
        return 0;
    }

    if (!gSdhostCardLost && SdhostProbeCardPresent()) {
        return 0;
    }

    gSdhostReady = 0;
    gSdhostCardLost = 1;
    SdhostAnnounceUnplug();
    return 1;
}
