/*
 * Usb.c — K10：USB 探控最小子集
 *
 * 【初学者】
 * 完整路径是 xHCI 复位 → 环 → 端口枚举 → HID/MSC。
 * 本刀只证明：Boot 找到了控制器基址，且内核能读到 CAPLENGTH / HCIVERSION。
 * 不建环、不开中断、不枚举设备。
 */
#include "Usb.h"
#include "BootInfo.h"
#include "BootTypes.h"
#include "HalSerial.h"
#include "IdentityMap.h"
#include "ToySerialConfig.h"

static UINT64 gXhciBase;
static int gXhciReady;

int UsbXhciReady(void) {
    return gXhciReady;
}

UINT64 UsbXhciBase(void) {
    return gXhciBase;
}

#if defined(__x86_64__) || defined(_M_X64)
/* 读能力区：CAPLENGTH(1) + HCIVERSION(2)；非法则判未就绪 */
static int ProbeXhciCaps(UINT64 Base, UINT8 *OutCap, UINT16 *OutVer) {
    volatile UINT8 *Mmio;
    UINT8 Cap;
    UINT16 Ver;

    if (Base == 0 || (Base & 0xFu) != 0) {
        return -1;
    }
    /* 恒等窗外勿碰：缺页/总线挂起会卡死串口 */
    if (Base >= TOY_IDENTITY_BYTES ||
        Base + 4ull > TOY_IDENTITY_BYTES) {
        return -2;
    }
    Mmio = (volatile UINT8 *)(UINTN)Base;
    Cap = Mmio[0];
    Ver = (UINT16)Mmio[2] | ((UINT16)Mmio[3] << 8);
    /* 经验窗：CAPLENGTH 通常 0x20..0x40；版本非全 0/全 F */
    if (Cap < 0x20u || Cap == 0xFFu || Ver == 0 || Ver == 0xFFFFu) {
        return -1;
    }
    if (OutCap != 0) {
        *OutCap = Cap;
    }
    if (OutVer != 0) {
        *OutVer = Ver;
    }
    return 0;
}
#endif

int UsbInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gXhciBase = 0;
    gXhciReady = 0;

#if defined(__x86_64__) || defined(_M_X64)
    {
        UINT8 Cap = 0;
        UINT16 Ver = 0;
        UINT64 Base = 0;
        int Probe;

        if (Info != 0) {
            Base = Info->XhciBase;
        }
        gXhciBase = Base;

        if (Base == 0) {
            HalSerialWriteChannel(TOY_SLOG_USB, "Usb: WARN no xhci (handoff)\n");
            return 0;
        }

        Probe = ProbeXhciCaps(Base, &Cap, &Ver);
        if (Probe == -2) {
            /* 基址在 4GiB 恒等窗外：handoff 仍算 ok；真 MMIO Map 后刀 */
            gXhciReady = 1;
            HalSerialWriteChannel(TOY_SLOG_USB, "Usb: xhci ok @0x");
            HalSerialWriteChannelHex64(TOY_SLOG_USB, Base);
            HalSerialWriteChannel(TOY_SLOG_USB, " (Boot handoff)\n");
            return 0;
        }
        if (Probe != 0) {
            HalSerialWriteChannel(TOY_SLOG_USB, "Usb: WARN xhci mmio bad @0x");
            HalSerialWriteChannelHex64(TOY_SLOG_USB, Base);
            HalSerialWriteChannel(TOY_SLOG_USB, "\n");
            return 0;
        }
        gXhciReady = 1;
        HalSerialWriteChannel(TOY_SLOG_USB, "Usb: xhci ok @0x");
        HalSerialWriteChannelHex64(TOY_SLOG_USB, Base);
        HalSerialWriteChannel(TOY_SLOG_USB, " cap=0x");
        HalSerialWriteChannelHex32(TOY_SLOG_USB, Cap);
        HalSerialWriteChannel(TOY_SLOG_USB, " ver=0x");
        HalSerialWriteChannelHex32(TOY_SLOG_USB, Ver);
        HalSerialWriteChannel(TOY_SLOG_USB, "\n");
        return 0;
    }
#else
    (void)Info;
    HalSerialWriteChannel(TOY_SLOG_USB, "Usb: stub (no xhci probe)\n");
    return 0;
#endif
}
