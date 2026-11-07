/*
 * UsbHostBringUp.c — PR-MIX2-USB-1：MSM8998 DWC3 最小 Host 探测
 *
 * 证据：线刷 DT `dwc3@a800000` / `snps,dwc3`（ssusb@a800000）。
 * 对表 Synopsys DWC3：GSNPSID@0xC120、GCTL@0xC110 PrtCapDir=Host。
 * 本刀只证 MMIO 可读并把能力切到 Host；hub/HID 后续刀。
 */
#include "Board.h"
#include "BoardConfig.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define DWC3_REGISTER_BASE        0x0A800000ULL
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

static void CopyText(char *Destination, const char *Source, UINTN Capacity) {
    UINTN Index;

    for (Index = 0; Index + 1u < Capacity && Source[Index] != '\0'; Index++) {
        Destination[Index] = Source[Index];
    }
    Destination[Index] = '\0';
}

static void AppendText(char *Destination, const char *Source, UINTN Capacity) {
    UINTN Length;

    for (Length = 0; Length < Capacity && Destination[Length] != '\0'; Length++) {
    }
    CopyText(Destination + Length, Source, Capacity - Length);
}

int BoardMix2UsbHostProbe(void) {
    UINT32 SynopsysId;
    UINT32 GlobalControl;
    char Line[48];
    char Hex[12];
    UINTN Guard;

    /* 紧贴 Blocks ready 下：小字行高约 28，避免出 logo 可视带 */
    BoardBringUpSplashPaintLine(108, "usb probe");

    SynopsysId = *Dwc3Register(DWC3_GSNPSID_OFFSET);
    FormatHexadecimal32(Hex, SynopsysId);
    CopyText(Line, "id ", sizeof(Line));
    AppendText(Line, Hex, sizeof(Line));
    BoardBringUpSplashPaintLine(108, Line);

    if ((SynopsysId & 0xFFFF0000u) != DWC3_GSNPSID_SYNOPSYS) {
        BoardBringUpSplashPaintLine(136, "no dwc3");
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
    FormatHexadecimal32(Hex, GlobalControl);
    CopyText(Line, "host ", sizeof(Line));
    AppendText(Line, Hex, sizeof(Line));
    BoardBringUpSplashPaintLine(136, Line);
    return 0;
}

#else

int BoardMix2UsbHostProbe(void) {
    return -1;
}

#endif
