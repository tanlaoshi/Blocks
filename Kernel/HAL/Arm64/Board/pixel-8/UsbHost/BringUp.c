/*
 * UsbHost/BringUp.c — PR-PIXEL8-USB-1：Tensor G3 DWC3 最小 Host 探测
 *
 * 证据：zuma `sys.usb.controller=11210000.dwc3`；真机 MMIO：
 *   gctl=0x00012004 gsts=0x7e800000 id=0x33313130 guid=0x0d036fff
 * GSNPSID 非 0x5533，以 GCTL 读回 Host 为准。状态同步画 splash。
 */
#include "Board.h"
#include "BoardConfig.h"
#include "HalSerial.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define DWC3_GCTL_OFFSET          0xC110u
#define DWC3_GSTS_OFFSET          0xC118u
#define DWC3_GSNPSID_OFFSET       0xC120u
#define DWC3_GUID_OFFSET          0xC12Cu
#define DWC3_GCTL_PRTCAPDIR_MASK  (3u << 12)
#define DWC3_GCTL_PRTCAP_HOST     (1u << 12)
#define DWC3_GCTL_PRTCAP_DEVICE   (2u << 12)
#define DWC3_GCTL_CORESOFTRESET   (1u << 11)
#define DWC3_GSNPSID_SYNOPSYS     0x55330000u

static volatile UINT32 *Dwc3Register(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(BOARD_USB_DWC3_PHYSICAL_BASE + (UINT64)Offset);
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

static void WriteLabeledHex(const char *Prefix, UINT32 Value) {
    char Hex[12];

    FormatHexadecimal32(Hex, Value);
    HalSerialWriteShell(Prefix);
    HalSerialWriteShell(Hex);
    HalSerialWriteShell("\r\n");
}

static void CopyText(char *Destination, const char *Source, UINT32 Capacity) {
    UINTN Index;

    if (Capacity == 0) {
        return;
    }
    for (Index = 0; Index + 1u < Capacity && Source[Index] != '\0'; Index++) {
        Destination[Index] = Source[Index];
    }
    Destination[Index] = '\0';
}

static void AppendText(char *Destination, const char *Source, UINT32 Capacity) {
    UINTN Length;

    Length = 0;
    while (Destination[Length] != '\0' && Length + 1u < Capacity) {
        Length++;
    }
    CopyText(Destination + Length, Source, Capacity - (UINT32)Length);
}

int BoardPixel8UsbHostProbe(void) {
    UINT32 SynopsysId;
    UINT32 GlobalControl;
    UINT32 GlobalStatus;
    UINT32 UserId;
    UINT32 PortCapability;
    UINTN Guard;
    char Line[48];
    char Hex[12];

    HalSerialWriteShell("usb: probe\r\n");
    BoardBringUpSplashPaintLine(112, "usb probe");

    GlobalControl = *Dwc3Register(DWC3_GCTL_OFFSET);
    GlobalStatus = *Dwc3Register(DWC3_GSTS_OFFSET);
    SynopsysId = *Dwc3Register(DWC3_GSNPSID_OFFSET);
    UserId = *Dwc3Register(DWC3_GUID_OFFSET);

    WriteLabeledHex("usb: gctl ", GlobalControl);
    WriteLabeledHex("usb: gsts ", GlobalStatus);
    WriteLabeledHex("usb: id ", SynopsysId);
    WriteLabeledHex("usb: guid ", UserId);

    FormatHexadecimal32(Hex, SynopsysId);
    CopyText(Line, "id ", sizeof(Line));
    AppendText(Line, Hex, sizeof(Line));
    BoardBringUpSplashPaintLine(112, Line);

    if (GlobalControl == 0u && GlobalStatus == 0u) {
        HalSerialWriteShell("usb: dead mmio\r\n");
        BoardBringUpSplashPaintLine(160, "dead mmio");
        return -1;
    }

    if ((SynopsysId & 0xFFFF0000u) != DWC3_GSNPSID_SYNOPSYS) {
        HalSerialWriteShell("usb: id-odd (GCTL live)\r\n");
    }

    PortCapability = GlobalControl & DWC3_GCTL_PRTCAPDIR_MASK;
    if (PortCapability == DWC3_GCTL_PRTCAP_DEVICE) {
        HalSerialWriteShell("usb: was device\r\n");
    }

    /*
     * id-odd 时跳过 CoreSoftReset（未证复位语义）；只改 PrtCapDir=Host。
     * 对表 dwc3 core：GCTL[13:12] 1=Host / 2=Device / 3=OTG。
     */
    if ((SynopsysId & 0xFFFF0000u) == DWC3_GSNPSID_SYNOPSYS) {
        HalSerialWriteShell("usb: rst\r\n");
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
    }

    HalSerialWriteShell("usb: cap\r\n");
    GlobalControl = *Dwc3Register(DWC3_GCTL_OFFSET);
    GlobalControl &= ~DWC3_GCTL_PRTCAPDIR_MASK;
    GlobalControl |= DWC3_GCTL_PRTCAP_HOST;
    *Dwc3Register(DWC3_GCTL_OFFSET) = GlobalControl;

    GlobalControl = *Dwc3Register(DWC3_GCTL_OFFSET);
    WriteLabeledHex("usb: host ", GlobalControl);
    FormatHexadecimal32(Hex, GlobalControl);
    CopyText(Line, "host ", sizeof(Line));
    AppendText(Line, Hex, sizeof(Line));
    BoardBringUpSplashPaintLine(160, Line);
    if ((GlobalControl & DWC3_GCTL_PRTCAPDIR_MASK) != DWC3_GCTL_PRTCAP_HOST) {
        HalSerialWriteShell("usb: host-fail\r\n");
        BoardBringUpSplashPaintLine(160, "host-fail");
        return -1;
    }
    return 0;
}

#else

int BoardPixel8UsbHostProbe(void) {
    return -1;
}

#endif
