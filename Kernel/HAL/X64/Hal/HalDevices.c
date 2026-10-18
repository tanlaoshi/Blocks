/*
 * HalDevices.c — x86：注册 / USB·MSC / Wifi·Iwl / Igpu·Hda（PR-S3-haldev-1）
 *
 * Common 只见 Hal*；Input/Net 门面见 HalDevicesInput.c / HalDevicesNet.c。
 */
#include "Hal.h"
#include "DriverBoot.h"
#include "UsbHid/InputXhci.h"
#include "Msc/UsbMsc.h"
#include "Ehci.h"
#include "XHCI.h"
#include "Rtl8188eu.h"
#include "Iwl.h"
#include "Igpu.h"
#include "Hda.h"

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

int HalUsbMscRelease(void) {
    return UsbMscRelease();
}

int HalUsbMscHotPoll(void) {
    return UsbMscHotPoll();
}

int HalUsbMscHot(void) {
    return UsbMscHot();
}

int HalUsbUartClaim(void) {
    int Rc;

    /* FTDI：先 xHCI，再 EHCI（N56VZ 棒在 RMH）；已认则 CDC 互斥跳过 */
    FtdiPublish();
    Rc = gFtdiUart.Claim ? gFtdiUart.Claim() : 0;
    if (Rc == 1) {
        return 1;
    }
    if (Ready()) {
        Rc = FtdiClaim();
        if (Rc == 1) {
            return 1;
        }
    }
    CdcPublish();
    return gCdcUart.Claim ? gCdcUart.Claim() : 0;
}

int HalUsbUartReady(void) {
    return ((gFtdiUart.Ready && gFtdiUart.Ready()) || FtdiReady() ||
            (gCdcUart.Ready && gCdcUart.Ready())) ? 1 : 0;
}

/* PR-N-wifi-1：USB 8188EU；先 xHCI 再 EHCI；无棒 0 */
int HalWifiClaim(void) {
    return WifiSetup() ? 1 : 0;
}

int HalWifiReady(void) {
    return WifiReady();
}

/* PR-N-wifi-1 / PR-BOOT-fast-3：放行后 PCI+BAR；FW/关联仍走 Worker BgPump；无卡 0 */
int HalIwlClaim(void) {
    IwlAllowBootClaim();
    return IwlSetup() ? 1 : 0;
}

int HalIwlReady(void) {
    return IwlReady();
}

int HalIwlAssociated(void) {
    return IwlAssociated();
}

int HalIwlBgBusy(void) {
    return IwlNetBgBusy();
}

void HalIwlBgPump(void) {
    IwlNetBgPump();
}

UINT64 HalIwlLogTsc(void) {
    return IwlLogTsc();
}

void HalIgpuMmioInitialize(void) {
    (void)IgpuMmioInit();
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

int HalIgpuPresentRect(const UINT32 *Back, UINT32 BackPitchPx, UINT32 FrontPitchPx,
                       UINT32 BackH, UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1) {
    return IgpuPresentRect(Back, BackPitchPx, FrontPitchPx, BackH, X0, Y0, X1, Y1);
}

int HalIgpuCopyRectBack(const UINT32 *Back, UINT32 PitchPx, UINT32 BufH,
                        UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                        UINT32 W, UINT32 H) {
    return IgpuCopyRectBack(Back, PitchPx, BufH, SrcX, SrcY, DstX, DstY, W, H);
}
