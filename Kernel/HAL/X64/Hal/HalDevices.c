/*
 * HalDevices.c — x86：注册 / USB·MSC / Wifi·Iwl / Igpu·Hda（PR-S3-haldev-1）
 *
 * Common 只见 Hal*；Input/Net 门面见 HalDevicesInput.c / HalDevicesNet.c。
 * 板载网门面（三架构同名）：有则 `BoardEthernetProbe`（`Ethernet.c`）；本 Arch 走 PCI NIC，不交板级空 stub。
 */
#include "Hal.h"
#include "DriverBoot.h"
#include "UsbHid/InputXhci.h"
#include "Msc/UsbMsc.h"
#include "Ehci.h"
#include "XHCI.h"
#include "Iwl.h"
#include "Igpu.h"
#include "Hda/Hda.h"

extern char __driver_boot_start[];
extern char __driver_boot_end[];

void HalDriverRegister(void) {
    /* 块设备按 Order 登记：先 AHCI，再 ATA，再 NVMe。后登记者 Probe 时可覆盖后端。 */
    const DRIVER_BOOT *Start = (const DRIVER_BOOT *)__driver_boot_start;
    const DRIVER_BOOT *End = (const DRIVER_BOOT *)__driver_boot_end;
    const DRIVER_BOOT *Item[32];
    const DRIVER_BOOT *P;
    int N = 0;
    int i;
    int j;

    for (P = Start; P < End && N < 32; P++) {
        Item[N++] = P;
    }
    for (i = 1; i < N; i++) {
        const DRIVER_BOOT *Key = Item[i];

        j = i;
        while (j > 0 && Item[j - 1]->Order > Key->Order) {
            Item[j] = Item[j - 1];
            j--;
        }
        Item[j] = Key;
    }
    for (i = 0; i < N; i++) {
        if (Item[i]->Call != 0) {
            Item[i]->Call();
        }
    }
}

int HalUsbInitialize(void) {
    return InputXhciInit();
}

int HalUsbMscInitialize(void) {
    return UsbMscInit();
}

int HalUsbMscReady(void) {
    return UsbMscReady();
}

int HalUsbMscScan(void) {
    return UsbMscScan();
}

int HalUsbMscClaim(void) {
    return UsbMscClaim();
}

int HalUsbMscCapacity(void) {
    return UsbMscCapacity();
}

UINT32 HalUsbMscBlockCount(void) {
    return UsbMscBlockCount();
}

UINT32 HalUsbMscBlockSize(void) {
    return UsbMscBlockSize();
}

int HalUsbMscMount(void) {
    return UsbMscMount();
}

int HalUsbMscAutoEnabled(void) {
    return UsbMscAutoEnabled();
}

void HalUsbMscAutoSet(int On) {
    UsbMscAutoSet(On);
}

int HalUsbMscAutoBeforeFs(void) {
    return UsbMscAutoBeforeFs();
}

static HAL_BOOT_LOGO_BUMP_FN gBootLogoBump;

void HalBootLogoBumpRegister(HAL_BOOT_LOGO_BUMP_FN Fn) {
    gBootLogoBump = Fn;
}

void HalBootLogoBump(UINT32 Cap) {
    if (gBootLogoBump) {
        gBootLogoBump(Cap);
    }
}

int HalUsbMscRelease(void) {
    return UsbMscRelease();
}

int HalUsbMscPollUnplug(void) {
    return UsbMscPollUnplug();
}

void HalUsbMscArmBootWatch(int On) {
    UsbMscArmBootWatch(On);
}

int HalUsbPortEnumIsFrozen(void) {
    return XhciPortEnumIsFrozen();
}

int HalUsbMscHotPoll(void) {
    return UsbMscHotPoll();
}

int HalUsbMscHot(void) {
    /* HotPoll 不解冻；仅显式 msc hot 允许再 Address */
    XhciPortEnumUnfreeze();
    return UsbMscHot();
}

int HalUsbUartClaim(void) {
    int Rc;

    /* FTDI：先 xHCI，已认则跳过；否则 EHCI 就绪时再认已发布的 FTDI；再 CDC */
    Rc = gFtdiUart.Claim ? gFtdiUart.Claim() : 0;
    if (Rc == 1) {
        return 1;
    }
    if (Ready() && gFtdiEhci.Claim) {
        Rc = gFtdiEhci.Claim();
        if (Rc == 1) {
            return 1;
        }
    }
    return gCdcUart.Claim ? gCdcUart.Claim() : 0;
}

int HalUsbUartReady(void) {
    return ((gFtdiUart.Ready && gFtdiUart.Ready()) ||
            (gFtdiEhci.Ready && gFtdiEhci.Ready()) ||
            (gCdcUart.Ready && gCdcUart.Ready())) ? 1 : 0;
}

/* PR-N-wifi-1：USB 8188EU；先 xHCI 再 EHCI；无棒 0 */
static HAL_WIFI_OPS gWifiOps;

void HalWifiSetOps(const HAL_WIFI_OPS *Ops) {
    if (Ops == 0) {
        return;
    }
    gWifiOps = *Ops;
}

int HalWifiClaim(void) {
    if (gWifiOps.Claim == 0) {
        return 0;
    }
    return gWifiOps.Claim();
}

int HalWifiReady(void) {
    if (gWifiOps.Ready == 0) {
        return 0;
    }
    return gWifiOps.Ready();
}

int HalWifiScan(void) {
    if (gWifiOps.Scan == 0) {
        return -1;
    }
    return gWifiOps.Scan();
}

UINT32 HalWifiScanCount(void) {
    if (gWifiOps.ScanCount == 0) {
        return 0;
    }
    return gWifiOps.ScanCount();
}

int HalWifiScanEntryAt(UINT32 Index, char *SsidOut, UINT32 SsidCapacity, INT32 *RssiOut,
                       UINT16 *ChannelOut) {
    if (gWifiOps.ScanEntryAt == 0) {
        return -1;
    }
    return gWifiOps.ScanEntryAt(Index, SsidOut, SsidCapacity, RssiOut, ChannelOut);
}

int HalWifiAssociateOpen(const char *Ssid) {
    if (gWifiOps.AssociateOpen == 0) {
        return -1;
    }
    return gWifiOps.AssociateOpen(Ssid);
}

/* PR-N-wifi-1 / PR-BOOT-fast-3：放行后 PCI+BAR；FW/关联仍走 Worker BgPump；无卡 0 */
static HAL_IWL_OPS gIwlOps;

void HalIwlSetOps(const HAL_IWL_OPS *Ops) {
    if (Ops == 0) {
        return;
    }
    gIwlOps = *Ops;
}

int HalIwlClaim(void) {
    if (gIwlOps.Claim == 0) {
        return 0;
    }
    return gIwlOps.Claim();
}

int HalIwlReady(void) {
    if (gIwlOps.Ready == 0) {
        return 0;
    }
    return gIwlOps.Ready();
}

int HalIwlAssociated(void) {
    if (gIwlOps.Associated == 0) {
        return 0;
    }
    return gIwlOps.Associated();
}

int HalIwlBgBusy(void) {
    if (gIwlOps.BgBusy == 0) {
        return 0;
    }
    return gIwlOps.BgBusy();
}

void HalIwlBgPump(void) {
    IwlNetBgPump();
}

UINT64 HalIwlLogTsc(void) {
    return IwlLogTsc();
}

int HalIgpuMmioInitialize(void) {
    return IgpuMmioInit();
}

void HalHdaMmioInitialize(void) {
    (void)HdaMmioInit();
}

void HalHdaCodecInitialize(void) {
    (void)HdaCodecInit();
}

void HalHdaStreamInitialize(void) {
    (void)HdaStreamInit();
}

int HalAudioProbe(void) {
    return HdaAudioProbe();
}

int HalAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                    UINT32 Channels, UINT32 Bits) {
    return HdaAudioPlayPcm(Samples, Bytes, RateHz, Channels, Bits);
}

void HalAudioStop(void) {
    HdaAudioStop();
}

void HalAudioBeep(void) {
    (void)HdaAudioPlayPcm(0, 0, 48000u, 2u, 16u);
}

void HalIgpuGttInitialize(void) {
    (void)IgpuGttInit();
}

void HalIgpuForcewakeInitialize(void) {
    (void)IgpuForcewakeInit();
}

void HalIgpuBlitInitialize(void) {
    (void)IgpuBlitInit();
}

void HalIgpuBlitColorTest(void) {
    (void)IgpuBlitColorTest();
}

void HalIgpuPresentPrepare(void) {
    IgpuPresentPrepare();
}

void HalIgpuPresentInvalidate(void) {
    IgpuPresentInvalidate();
}

int HalIgpuReady(void) {
    /* igpu-6/7：探针 CopyOk 才 GPU Present；翻页另见 IgpuScanoutOk */
    return IgpuReady() && IgpuPresentCopyOk();
}

UINT32 HalIgpuBlitMinPixels(void) {
    return IgpuBlitMinPixels();
}

void HalIgpuNotePresentSkipScale(void) {
    IgpuNotePresentSkipScale();
}

void HalIgpuPresentSetLivePatchOnly(int On) {
    IgpuPresentSetLivePatchOnly(On);
}

int HalIgpuPresentRect(const UINT32 *Back, UINT32 BackPitchPx, UINT32 FrontPitchPx,
                       UINT32 BackH, UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1) {
    return IgpuPresentRect(Back, BackPitchPx, FrontPitchPx, BackH, X0, Y0, X1, Y1);
}

int HalIgpuCopyRectBack(const UINT32 *Back, UINT32 PitchPx, UINT32 BufH,
                        UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                        UINT32 W, UINT32 H) {
    return IgpuCopyRectBack(Back, PitchPx, BufH, SrcX, SrcY, DstX, DstY, W, H);
}
