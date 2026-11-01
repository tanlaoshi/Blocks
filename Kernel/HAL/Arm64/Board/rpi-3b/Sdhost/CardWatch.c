/*
 * CardWatch.c — Pi µSD 拔卡盯梢（对表 X64 usb-boot-unplug）
 *
 * Worker→HalUsbMscPollUnplug→本函数：CMD13 连续失败 → 钉 BootMediumGone →
 * Clock 画大窗倒数关机。有 I/O 忙则本拍跳过，避免与 PIO 抢控制器。
 */
#include "Sdhost/Private.h"
#include "FileSystem.h"
#include "SerialLog.h"

static int SdhostStatusCommandOk(void) {
    /* CMD13 SEND_STATUS：卡在则 R1；拔走则超时/CRC → 非 0 */
    return SdhostCommandSend(13, gSdhostRelativeCardAddress, RESPONSE_R1, 0, 0) == 0
               ? 1
               : 0;
}

/*
 * 1 = 本拍已确认 µSD 失根（已 Notify + Ready=0）。
 * 节流 ~2s；连续 2 拍缺席才认拔（对表 MSC Present streak）。
 */
int BoardSdhostPollUnplug(void) {
    static UINT64 LastCheckTick;
    static UINT32 AbsentStreak;
    UINT64 Now;
    UINT32 TicksPerSecond;

    if (!gSdhostReady) {
        return 0;
    }

    Now = HalCpuTicks(0);
    TicksPerSecond = HalTicksPerSec();
    if (TicksPerSecond == 0) {
        TicksPerSecond = 250;
    }
    if (LastCheckTick != 0 &&
        (Now - LastCheckTick) < (UINT64)TicksPerSecond * 2u) {
        return 0;
    }
    LastCheckTick = Now;

    if (gSdhostIoBusy) {
        return 0;
    }

    if (!gSdhostCardLost && SdhostStatusCommandOk()) {
        AbsentStreak = 0;
        return 0;
    }

    AbsentStreak++;
    if (AbsentStreak < 2u && !gSdhostCardLost) {
        return 0;
    }

    AbsentStreak = 0;
    gSdhostReady = 0;
    gSdhostCardLost = 1;
    LogFs("FS: sdhost unplug (boot medium removed)\n");
    FileSystemNotifyBootWatchUnplug();
    return 1;
}
