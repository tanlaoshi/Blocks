/*
 * UsbMsc.c — BOT 门面（story-s3 / PR-H-msc-*；主机表 UsbMscHost）
 *
 * 稳态 S3 设备侧：
 *   开机：HalUsbMscAutoBeforeFs → Claim/Mount(Mux)（FileSystemInitialize 序 5）
 *   HotPoll（Worker ~2Hz）：拔出→Release；热插→Scan 后 Claim/Mount（认 gMscAuto）
 *   UsbMscHot：Shell msc hot（不认 auto；拔卸或 claim）
 *   就绪：UsbMscReady / HalUsbMscReady（Shell msc、BlockMsc）
 * Init/Scan/Claim/Capacity/Mount；主机优先 xHCI，否则 EHCI。
 */
#include "Msc/UsbMsc.h"
#include "UsbMscHost.h"
#include "Block.h"
#include "BlockMux.h"
#include "SerialLog.h"
#include "Debug.h"
#include "Hal.h"

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
        LogBoot("Boot: MSC Auto No HC\n");
        return -1;
    }
    if (Claim == 0) {
        return 1;
    }

    Rc = UsbMscMount();
    if (Rc != 0) {
        LogBoot("Boot: MSC Auto Mux Fail\n");
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
    int N;
    int Claim;
    int Rc;
    static UINT64 LastPlugScan;
    UINT64 Now;
    UINT32 Tps;

    /* 拔出：已 claim 且 CCS/hub 不在 → Release，调用方 Remount */
    if (UsbMscReady()) {
        H = CurHost();
        Present = (H && H->Present) ? H->Present() : 0;
        if (Present) {
            return 0;
        }
        DebugWrite("msc: hot unplug → release\n");
        (void)UsbMscRelease();
        return 1;
    }

    /*
     * 热插（story-s3-2）：仅 auto；先 Scan（PORTSC，不 Address）再 Claim。
     * 空闲节流 ~5s，避免 2Hz Claim 抢 xHCI（NUC Address 超时→MSI stall）。
     */
    if (!UsbMscAutoEnabled()) {
        return 0;
    }
    Now = HalCpuTicks(0);
    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 250;
    }
    if (LastPlugScan != 0 && (Now - LastPlugScan) < (UINT64)Tps * 5u) {
        return 0;
    }
    LastPlugScan = Now;

    N = UsbMscScan();
    if (N <= 0) {
        return 0;
    }
    Claim = UsbMscClaim();
    if (Claim <= 0) {
        return 0;
    }
    Rc = UsbMscMount();
    if (Rc != 0) {
        (void)UsbMscRelease();
        return 0;
    }
    DebugWrite("msc: hot plug → mount\n");
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
