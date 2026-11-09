/*
 * UsbMscHotplug.c — MSC 热插 / 拔 / boot-watch
 *
 * 从 UsbMsc.c 纯搬。注释即红线：freeze 勿 Scan；CCS 毛刺；Present 要连缺两次。
 */
#include "Msc/UsbMsc.h"
#include "UsbMscPrivate.h"
#include "SerialLog.h"
#include "Hal.h"
#include "XHCI.h" /* XhciPortEnumerateIsFrozen：freeze 时勿 Scan 抢鼠 */

/* Services：Release 前判启动盘；勿在 RemoveMsc 之后调（盘号 KIND 已清） */
void FileSystemNotifyMscUnplug(void);
void FileSystemNotifyBootWatchUnplug(void);
void XhciMscArmBootWatch(int On);
int XhciMscBootWatchCcsLost(UINT32 *OutPort);
void XhciMscBootWatchClear(void);

/*
 * 已认领 MSC 丢 Present → Notify+Release→1。
 * 根口只读 PORTSC；hub 子口 HubGetPortStatus（EP0）——仍 5s 节流。
 * Clock 在指针忙时也调本函数：失根大窗优先于空拖让路。
 */
void UsbMscArmBootWatch(int On) {
    XhciMscArmBootWatch(On);
}

int UsbMscPollUnplug(void) {
    const MSC_HOST *H;
    int Present;
    static UINT64 sPresentTick;
    UINT64 Now;
    UINT32 Tps;
    UINT32 WatchPort;

    Now = HalCpuTicks(0);
    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 250;
    }
    if (sPresentTick != 0 && (Now - sPresentTick) < (UINT64)Tps * 5u) {
        return 0;
    }
    sPresentTick = Now;

    /*
     * U 盘起 boot-watch：claim 成败都盯根口 CCS（只读 PORTSC）。
     * hub 后 MSC claim 成功后仍 keep watch——整根拔时 Present 可能假在位。
     * Ready 且 Present：CCS 毛刺忽略，保持 watch。
     */
    if (XhciMscBootWatchCcsLost(&WatchPort)) {
        if (UsbMscReady()) {
            H = CurrentHost();
            Present = (H && H->Present) ? H->Present() : 0;
            if (Present) {
                LogFs("FS: msc/4 watch CCS glitch ignored port=");
                LogFsHex32(WatchPort);
                LogFs("\n");
                return 0;
            }
        }
        LogFs("FS: msc/4 watch unplug port=");
        LogFsHex32(WatchPort);
        LogFs("\n");
        XhciMscBootWatchClear();
        FileSystemNotifyBootWatchUnplug();
        if (UsbMscReady()) {
            FileSystemNotifyMscUnplug();
            (void)UsbMscRelease();
        }
        return 1;
    }

    if (!UsbMscReady()) {
        return 0;
    }
    H = CurrentHost();
    Present = (H && H->Present) ? H->Present() : 0;
    {
        static UINT32 sAbsentStreak;

        if (Present) {
            sAbsentStreak = 0;
            return 0;
        }
        /* Present 单次 0：EP0/邻口毛刺；~5s×2 才 Release */
        sAbsentStreak++;
        if (sAbsentStreak < 2u) {
            return 0;
        }
        sAbsentStreak = 0;
    }
    LogFs("FS: msc/4 hot unplug\n");
    XhciMscBootWatchClear();
    FileSystemNotifyMscUnplug();
    (void)UsbMscRelease();
    return 1;
}

int UsbMscHotPoll(void) {
    int ScanN;
    int Claim;
    int Rc;
    static UINT64 sPlugTryTick;
    UINT64 Now;
    UINT32 Tps;

    /* 拔：与 PollUnplug 同路径（Clock 也可能已先调过） */
    if (UsbMscPollUnplug() == 1) {
        return 1;
    }
    if (UsbMscReady()) {
        return 0;
    }

    /* 热插：仅 Worker 调本函数；开机路径不进这里 */
    if (!UsbMscAutoEnabled()) {
        return 0;
    }
    /*
     * freeze 后 Claim 必拒：再 Scan（含 HubGetPortStatus）只抢鼠环。
     * 插盘用 Shell `msc hot`（Unfreeze）。
     */
    if (XhciPortEnumerateIsFrozen()) {
        return 0;
    }
    Now = HalCpuTicks(0);
    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 250;
    }
    /* ~5s 节流：Scan/Claim 含 Address，勿跟 2Hz 钟一起打 */
    if (sPlugTryTick != 0 && (Now - sPlugTryTick) < (UINT64)Tps * 5u) {
        return 0;
    }
    sPlugTryTick = Now;

    ScanN = UsbMscScan();
    if (ScanN <= 0) {
        return 0;
    }
    LogFs("FS: msc/4 hotplug scan=");
    LogFsHex32((UINT32)ScanN);
    LogFs("\n");

    /* ClaimPorts 见 freeze 直接 0；不解冻（保鼠）。插盘用 msc hot */
    Claim = UsbMscClaim();
    if (Claim < 0) {
        LogFs("FS: msc/4 hotplug claim abort\n");
        return 0;
    }
    if (Claim == 0) {
        LogFs("FS: msc/4 hotplug claim empty\n");
        return 0;
    }
    Rc = UsbMscMount();
    if (Rc != 0) {
        LogFs("FS: msc/4 hotplug mount fail\n");
        (void)UsbMscRelease();
        return 0;
    }
    LogFs("FS: msc/4 hotplug ok\n");
    return 1;
}

int UsbMscHot(void) {
    int Claim;
    int Rc;
    int Present;
    const MSC_HOST *H;

    if (UsbMscReady()) {
        H = CurrentHost();
        Present = (H && H->Present) ? H->Present() : 0;
        if (Present) {
            return 0;
        }
        FileSystemNotifyMscUnplug();
        (void)UsbMscRelease();
    }

    Claim = UsbMscClaim();
    if (Claim < 0) {
        return -1;
    }
    if (Claim == 0) {
        return 1;
    }
    Rc = UsbMscMount();
    if (Rc != 0) {
        (void)UsbMscRelease();
        return Rc < 0 ? Rc : -2;
    }
    return 0;
}
