/*
 * UsbMsc.c — MSC 门面（usb-map / story-s3；主机表 UsbMscHost）
 *
 * ```
 * 开机 AutoBeforeFs → Claim/Mount（策略；不搅热插）
 * Worker DesktopWorkerTick ~2Hz → HotPoll（仅桌面后）
 *   拔：Ready && !Present → NotifyMscUnplug（Mux 仍在）→ Release →1
 *       → Clock：Gone? 大窗 5s 关机 : Remount+Files
 *   插：!Ready && auto → Scan>0（含 hub）→ Claim/Mount →1；~5s 节流
 * Shell msc hot / EnsureSecondary → UsbMscHot（显式 claim）
 * ```
 * 日志：`Fs: msc/1..4` → BOOT/RUNTIME（角色见路线图 msc-desktop-hotplug）。
 * 失根：Notify 在 Release 前钉 Gone（BootDiskGuid 卷在 MSC 盘号）。
 */
#include "Msc/UsbMsc.h"
#include "UsbMscHost.h"
#include "Block.h"
#include "BlockMux.h"
#include "SerialLog.h"
#include "Debug.h"
#include "Hal.h"

/* Services：Release 前判启动盘；勿在 RemoveMsc 之后调（盘号 KIND 已清） */
void FileSystemNotifyMscUnplug(void);

void BlockMscInstall(void); /* BlockMsc.c */

#ifndef MSC_AUTO_DEFAULT
#define MSC_AUTO_DEFAULT 1 /* Live 默认开 */
#endif

static int gMscAuto = MSC_AUTO_DEFAULT;
static int gMscBe = 0; /* MSC_HOST_XHCI / MSC_HOST_EHCI / 0 */

static const MSC_HOST *CurHost(void) {
    if (!gMscBe) {
        return 0;
    }
    return UsbMscHostByKind(gMscBe);
}

static int BeReady(void) {
    const MSC_HOST *H = CurHost();

    return (H && H->Ready()) ? 1 : 0;
}

int UsbMscInit(void) {
    UINTN i;

    for (i = 0; i < UsbMscHostCount(); i++) {
        const MSC_HOST *H = UsbMscHostGet(i);

        if (H && H->BringUp) {
            (void)H->BringUp();
        }
    }
    return BeReady() ? 0 : -1;
}

int UsbMscReady(void) {
    UINTN i;

    if (BeReady()) {
        return 1;
    }
    for (i = 0; i < UsbMscHostCount(); i++) {
        const MSC_HOST *H = UsbMscHostGet(i);

        if (H && H->Ready && H->Ready()) {
            gMscBe = H->Kind;
            return 1;
        }
    }
    return 0;
}

int UsbMscScan(void) {
    UINTN i;
    int Sum = 0;
    int AnyHc = 0;

    for (i = 0; i < UsbMscHostCount(); i++) {
        const MSC_HOST *H = UsbMscHostGet(i);
        int N;

        if (!H || !H->Scan) {
            continue;
        }
        if (H->ControllerReady && !H->ControllerReady()) {
            continue;
        }
        AnyHc = 1;
        N = H->Scan();
        if (N < 0) {
            continue;
        }
        Sum += N;
    }
    if (!AnyHc && Sum == 0) {
        return -1;
    }
    return Sum;
}

int UsbMscClaim(void) {
    UINTN i;
    int Last = 0;
    int SawHc = 0;

    if (UsbMscReady()) {
        return 1;
    }

    for (i = 0; i < UsbMscHostCount(); i++) {
        const MSC_HOST *H = UsbMscHostGet(i);
        int Rc;

        if (!H || !H->Claim) {
            continue;
        }
        if (H->ControllerReady && !H->ControllerReady()) {
            continue;
        }
        SawHc = 1;
        Rc = H->Claim();
        if (Rc > 0) {
            gMscBe = H->Kind;
            return Rc;
        }
        Last = Rc;
    }
    if (!SawHc) {
        return -1;
    }
    return Last;
}

static const MSC_HOST *HostForIo(void) {
    const MSC_HOST *H;
    const MSC_HOST *Ehci;

    if (gMscBe == MSC_HOST_EHCI) {
        return UsbMscHostByKind(MSC_HOST_EHCI);
    }
    /* 与旧逻辑一致：未记后端且 EHCI 已 Ready 时走 EHCI */
    Ehci = UsbMscHostByKind(MSC_HOST_EHCI);
    if (!gMscBe && Ehci && Ehci->Ready && Ehci->Ready()) {
        gMscBe = MSC_HOST_EHCI;
        return Ehci;
    }
    H = UsbMscHostByKind(MSC_HOST_XHCI);
    if (H) {
        gMscBe = MSC_HOST_XHCI;
    }
    return H;
}

int UsbMscCapacity(void) {
    const MSC_HOST *H = HostForIo();

    if (!H || !H->Capacity) {
        return -1;
    }
    return H->Capacity();
}

UINT32 UsbMscBlockCount(void) {
    const MSC_HOST *H;
    const MSC_HOST *Ehci = UsbMscHostByKind(MSC_HOST_EHCI);

    if (gMscBe == MSC_HOST_EHCI || (Ehci && Ehci->Ready && Ehci->Ready())) {
        H = Ehci;
    } else {
        H = UsbMscHostByKind(MSC_HOST_XHCI);
    }
    return (H && H->BlockCount) ? H->BlockCount() : 0;
}

UINT32 UsbMscBlockSize(void) {
    const MSC_HOST *H;
    const MSC_HOST *Ehci = UsbMscHostByKind(MSC_HOST_EHCI);

    if (gMscBe == MSC_HOST_EHCI || (Ehci && Ehci->Ready && Ehci->Ready())) {
        H = Ehci;
    } else {
        H = UsbMscHostByKind(MSC_HOST_XHCI);
    }
    return (H && H->BlockSize) ? H->BlockSize() : 0;
}

int UsbMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    const MSC_HOST *H;
    const MSC_HOST *Ehci = UsbMscHostByKind(MSC_HOST_EHCI);

    if (gMscBe == MSC_HOST_EHCI || (Ehci && Ehci->Ready && Ehci->Ready())) {
        H = Ehci;
    } else {
        H = UsbMscHostByKind(MSC_HOST_XHCI);
    }
    if (!H || !H->ReadSectors) {
        return -1;
    }
    return H->ReadSectors(Lba, Count, Buffer);
}

int UsbMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    const MSC_HOST *H;
    const MSC_HOST *Ehci = UsbMscHostByKind(MSC_HOST_EHCI);

    if (gMscBe == MSC_HOST_EHCI || (Ehci && Ehci->Ready && Ehci->Ready())) {
        H = Ehci;
    } else {
        H = UsbMscHostByKind(MSC_HOST_XHCI);
    }
    if (!H || !H->WriteSectors) {
        return -1;
    }
    return H->WriteSectors(Lba, Count, Buffer);
}

int UsbMscFlush(void) {
    const MSC_HOST *H;
    const MSC_HOST *Ehci = UsbMscHostByKind(MSC_HOST_EHCI);

    if (gMscBe == MSC_HOST_EHCI || (Ehci && Ehci->Ready && Ehci->Ready())) {
        H = Ehci;
    } else {
        H = UsbMscHostByKind(MSC_HOST_XHCI);
    }
    if (!H || !H->Flush) {
        return 1;
    }
    return H->Flush();
}

int UsbMscAutoEnabled(void) {
    return gMscAuto ? 1 : 0;
}

void UsbMscAutoSet(int On) {
    gMscAuto = On ? 1 : 0;
}

int UsbMscMount(void) {
    UINT32 Bsz;

    if (!UsbMscReady()) {
        return -1;
    }
    if (UsbMscBlockCount() == 0 || UsbMscBlockSize() == 0) {
        if (UsbMscCapacity() < 0) {
            return -2;
        }
    }
    Bsz = UsbMscBlockSize();
    if (Bsz != BLOCK_SECTOR_SIZE) {
        return -3;
    }
    BlockMscInstall();
    return 0;
}

int UsbMscAutoBeforeFs(void) {
    int Claim;
    int Rc;

    if (!UsbMscAutoEnabled()) {
        return 1;
    }

    Claim = UsbMscClaim();
    if (Claim < 0) {
        /* 无 HC 或 claim 中 cmd sick（已 Recover，勿当「空」再扫） */
        LogBoot("Boot: MSC Auto claim abort\n");
        return -1;
    }
    if (Claim == 0) {
        return 1;
    }

    Rc = UsbMscMount();
    if (Rc != 0) {
        LogBoot("Boot: MSC Auto Mux Fail\n");
        (void)UsbMscRelease();
        return Rc;
    }
    LogBoot("Boot: MSC Auto Mux OK\n");
    return 0;
}

int UsbMscRelease(void) {
    const MSC_HOST *H = CurHost();

    BlockMuxRemoveMsc();
    if (H && H->Release) {
        (void)H->Release();
    }
    gMscBe = 0;
    return 0;
}

int UsbMscHotPoll(void) {
    const MSC_HOST *H;
    int Present;
    int ScanN;
    int Claim;
    int Rc;
    static UINT64 sPlugTryTick;
    UINT64 Now;
    UINT32 Tps;

    /* 拔出 */
    if (UsbMscReady()) {
        H = CurHost();
        Present = (H && H->Present) ? H->Present() : 0;
        if (Present) {
            return 0;
        }
        LogFs("Fs: msc/4 hot unplug\n");
        FileSystemNotifyMscUnplug();
        (void)UsbMscRelease();
        return 1;
    }

    /* 热插：仅 Worker 调本函数；开机路径不进这里 */
    if (!UsbMscAutoEnabled()) {
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
    LogFs("Fs: msc/4 hotplug scan=");
    LogFsHex32((UINT32)ScanN);
    LogFs("\n");

    /* ClaimPorts 见 freeze 直接 0；不解冻（保鼠）。插盘用 msc hot */
    Claim = UsbMscClaim();
    if (Claim < 0) {
        LogFs("Fs: msc/4 hotplug claim abort\n");
        return 0;
    }
    if (Claim == 0) {
        LogFs("Fs: msc/4 hotplug claim empty\n");
        return 0;
    }
    Rc = UsbMscMount();
    if (Rc != 0) {
        LogFs("Fs: msc/4 hotplug mount fail\n");
        (void)UsbMscRelease();
        return 0;
    }
    LogFs("Fs: msc/4 hotplug ok\n");
    return 1;
}

int UsbMscHot(void) {
    int Claim;
    int Rc;
    int Present;
    const MSC_HOST *H;

    if (UsbMscReady()) {
        H = CurHost();
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
