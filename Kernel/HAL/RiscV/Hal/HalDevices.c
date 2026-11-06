/*
 * HalDevices.c — RiscV：注册 / Block·USB 桩 / Wifi·Iwl / Igpu·Hda（PR-S3-haldev-1）
 *
 * Common 只见 Hal*；Input/Net 见 HalDevicesInput.c / HalDevicesNet.c。
 * 板包用 BOARD_HAS_* 勾选；Duo S 等命令行靶不探 virtio（避免误扫 MMIO）。
 */
#include "Hal.h"
#include "HalDevices.h"
#include "BoardConfig.h"
#include "Block.h"
#include "Driver.h"
#if BOARD_IS_VIRT && BOARD_HAS_BLOCK
#include "Virtio/BlockDevice.h"
#endif
#if BOARD_IS_VIRT && BOARD_HAS_FRAMEBUFFER
#include "Virtio/Input.h"
#endif
#if BOARD_IS_VIRT && BOARD_HAS_NET
#include "Virtio/Net/Net.h"
#endif

void HalDriverRegister(void) {
#if BOARD_IS_VIRT
#if BOARD_HAS_BLOCK
    VirtioBlockRegister();
#endif
#if BOARD_HAS_FRAMEBUFFER
    VirtioInputRegister();
#endif
#if BOARD_HAS_NET
    VirtioNetRegister();
#endif
#else
    {
        void BoardSdhciRegister(void);
        void BoardEthernetRegister(void);
        void BoardWifiRegister(void);

        BoardSdhciRegister();
        /* 板载网：Register；Probe/Bind 在 HalNetInitialize（BOARD_HAS_NET） */
        BoardEthernetRegister();
        /* Duo Wi‑Fi：对表 Pi BoardWifiRegister → Worker HalWifiClaim */
        BoardWifiRegister();
    }
#endif
}

int HalBlockInitialize(void) {
#if BOARD_IS_VIRT
#if BOARD_HAS_BLOCK
    return VirtioBlockInitialize();
#else
    return 0;
#endif
#else
    /* Duo S：cv1800-sdhci → DriverBlockAttach → BlockInitialize */
    (void)DriverProbeClass(DRIVER_CLASS_BLOCK);
    if (!BlockBackendReady()) {
        return 0;
    }
    return BlockInitialize();
#endif
}

int HalUsbInitialize(void) {
#if BOARD_IS_VIRT && BOARD_HAS_FRAMEBUFFER
    return VirtioInputInitialize();
#elif !BOARD_IS_VIRT
    /* Duo S：DWC2 host + HID boot 键（duo-usb-kbd）；无设备软成功 */
    {
        int BoardUsbHostControllerProbe(void);
        (void)BoardUsbHostControllerProbe();
    }
    return 0;
#else
    return 0;
#endif
}

int HalUsbMscInitialize(void) {
    return -1;
}

int HalUsbMscReady(void) {
    return 0;
}

int HalUsbMscScan(void) {
    return -1;
}

int HalUsbMscClaim(void) {
    return -1;
}

int HalUsbMscCapacity(void) {
    return -1;
}

UINT32 HalUsbMscBlockCount(void) {
    return 0;
}

UINT32 HalUsbMscBlockSize(void) {
    return 0;
}

int HalUsbMscMount(void) {
    return -1;
}

int HalUsbMscAutoEnabled(void) {
    return 0;
}

void HalUsbMscAutoSet(int On) {
    (void)On;
}

int HalUsbMscAutoBeforeFs(void) {
    return 1;
}

void HalBootLogoBumpRegister(HAL_BOOT_LOGO_BUMP_FN Fn) {
    (void)Fn;
}

void HalBootLogoBump(UINT32 Cap) {
    (void)Cap;
}

int HalUsbMscRelease(void) {
    return 0;
}

int HalUsbMscPollUnplug(void) {
    return 0;
}

void HalUsbMscArmBootWatch(int On) {
    (void)On;
}

int HalUsbPortEnumerateIsFrozen(void) {
    return 0;
}

int HalUsbMscHotPoll(void) {
    return 0;
}

int HalUsbMscHot(void) {
    return 1;
}

int HalUsbUartClaim(void) {
    return -1;
}

int HalUsbUartReady(void) {
    return 0;
}

/* 板包 BoardWifiRegister → HalWifiSetOps；virt / 未登记 → 0 / -1 */
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

int HalIwlClaim(void) {
    return 0;
}

int HalIwlReady(void) {
    return 0;
}

int HalIwlAssociated(void) {
    return 0;
}

int HalIwlBgBusy(void) {
    return 0;
}

void HalIwlBgPump(void) {
}

UINT64 HalIwlLogTsc(void) {
    return 0;
}

int HalIgpuMmioInitialize(void) {
    return 0;
}

void HalHdaMmioInitialize(void) {
}

void HalHdaCodecInitialize(void) {
}

void HalHdaStreamInitialize(void) {
}

#ifndef BOARD_HAS_AUDIO
#define BOARD_HAS_AUDIO 0
#endif

/* audio-duo-1：板级实现在 Board/milk-v-duo-s/Audio/ */
int DuoAudioProbe(void);
int DuoAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                    UINT32 Channels, UINT32 Bits);
void DuoAudioStop(void);
void DuoAudioBeep(void);

int HalAudioProbe(void) {
#if BOARD_HAS_AUDIO
    return DuoAudioProbe();
#else
    return 0;
#endif
}

int HalAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                    UINT32 Channels, UINT32 Bits) {
#if BOARD_HAS_AUDIO
    return DuoAudioPlayPcm(Samples, Bytes, RateHz, Channels, Bits);
#else
    (void)Samples;
    (void)Bytes;
    (void)RateHz;
    (void)Channels;
    (void)Bits;
    return 0;
#endif
}

void HalAudioStop(void) {
#if BOARD_HAS_AUDIO
    DuoAudioStop();
#endif
}

void HalAudioBeep(void) {
#if BOARD_HAS_AUDIO
    DuoAudioBeep();
#endif
}

void HalIgpuGttInitialize(void) {
}

void HalIgpuForcewakeInitialize(void) {
}

void HalIgpuBlitInitialize(void) {
}

void HalIgpuBlitColorTest(void) {
}

void HalIgpuPresentPrepare(void) {
}

void HalIgpuPresentInvalidate(void) {
}

int HalIgpuReady(void) {
    return 0;
}

UINT32 HalIgpuBlitMinPixels(void) {
    return 0;
}

void HalIgpuNotePresentSkipScale(void) {
}

void HalIgpuPresentSetLivePatchOnly(int On) {
    (void)On;
}

int HalIgpuPresentRect(const UINT32 *Back, UINT32 BackPitchPx, UINT32 FrontPitchPx,
                       UINT32 BackH, UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1) {
    (void)Back;
    (void)BackPitchPx;
    (void)FrontPitchPx;
    (void)BackH;
    (void)X0;
    (void)Y0;
    (void)X1;
    (void)Y1;
    return 0;
}

int HalIgpuCopyRectBack(const UINT32 *Back, UINT32 PitchPx, UINT32 BufH,
                        UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                        UINT32 W, UINT32 H) {
    (void)Back;
    (void)PitchPx;
    (void)BufH;
    (void)SrcX;
    (void)SrcY;
    (void)DstX;
    (void)DstY;
    (void)W;
    (void)H;
    return 0;
}
