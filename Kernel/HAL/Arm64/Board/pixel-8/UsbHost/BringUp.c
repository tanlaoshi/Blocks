/*
 * UsbHost/BringUp.c — PR-PIXEL8-USB-1：Tensor G3 DWC3 最小 Host 探测
 *
 * 证据：device/google/zuma `init.zuma.usb.rc` → `sys.usb.controller=11210000.dwc3`
 * （Linux OF 名 = reg 基址 + 结点名；即 snps,dwc3 @0x11210000）。
 * 对表 Synopsys DWC3：GSNPSID@0xC120、GCTL@0xC110 PrtCapDir=Host。
 * 本刀只证 MMIO 可读并切 Host；不碰 Type-C/PHY（Cereal SBU 串口须保留）。
 */
#include "Board.h"
#include "BoardConfig.h"
#include "HalSerial.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define DWC3_REGISTER_BASE        0x11210000ULL
#define DWC3_GCTL_OFFSET          0xC110u
#define DWC3_GSNPSID_OFFSET       0xC120u
#define DWC3_GCTL_PRTCAPDIR_MASK  (3u << 12)
#define DWC3_GCTL_PRTCAP_HOST     (1u << 12)
#define DWC3_GCTL_CORESOFTRESET   (1u << 11)
#define DWC3_GSNPSID_SYNOPSYS     0x55330000u

static volatile UINT32 *Dwc3Register(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(DWC3_REGISTER_BASE + (UINT64)Offset);
}

static void FormatHexadecimal32(char *Out, UINT32 Value) {
    static const char Digits[] = "0123456789abcdef";
    int Index;

    Out[0] = '0';
    Out[1] = 'x';
    for (Index = 0; Index < 8; Index++) {
        Out[2 + Index] = Digits[(Value >> (28 - Index * 4)) & 0xFu];
    }
    Out[10] = '\0';
}

static void WriteProbeLine(const char *Prefix, UINT32 Value) {
    char Hex[12];

    FormatHexadecimal32(Hex, Value);
    HalSerialWriteShell(Prefix);
    HalSerialWriteShell(Hex);
    HalSerialWriteShell("\r\n");
}

int BoardPixel8UsbHostProbe(void) {
    UINT32 SynopsysId;
    UINT32 GlobalControl;
    UINTN Guard;

    HalSerialWriteShell("usb: probe\r\n");

    SynopsysId = *Dwc3Register(DWC3_GSNPSID_OFFSET);
    WriteProbeLine("usb: id ", SynopsysId);

    if ((SynopsysId & 0xFFFF0000u) != DWC3_GSNPSID_SYNOPSYS) {
        HalSerialWriteShell("usb: no dwc3\r\n");
        return -1;
    }

    GlobalControl = *Dwc3Register(DWC3_GCTL_OFFSET);
    GlobalControl |= DWC3_GCTL_CORESOFTRESET;
    *Dwc3Register(DWC3_GCTL_OFFSET) = GlobalControl;
    for (Guard = 0; Guard < 100000u; Guard++) {
        __asm__ volatile("yield" ::: "memory");
    }
    GlobalControl = *Dwc3Register(DWC3_GCTL_OFFSET);
    GlobalControl &= ~DWC3_GCTL_CORESOFTRESET;
    *Dwc3Register(DWC3_GCTL_OFFSET) = GlobalControl;
    for (Guard = 0; Guard < 100000u; Guard++) {
        __asm__ volatile("yield" ::: "memory");
    }

    GlobalControl = *Dwc3Register(DWC3_GCTL_OFFSET);
    GlobalControl &= ~DWC3_GCTL_PRTCAPDIR_MASK;
    GlobalControl |= DWC3_GCTL_PRTCAP_HOST;
    *Dwc3Register(DWC3_GCTL_OFFSET) = GlobalControl;

    GlobalControl = *Dwc3Register(DWC3_GCTL_OFFSET);
    WriteProbeLine("usb: host ", GlobalControl);
    return 0;
}

#else

int BoardPixel8UsbHostProbe(void) {
    return -1;
}

#endif
