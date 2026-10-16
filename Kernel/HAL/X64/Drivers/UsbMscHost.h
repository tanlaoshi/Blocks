/*
 * UsbMscHost.h — USB MSC 主机表（UsbMsc ↔ xHCI/EHCI，互不 include 总头）
 */
#ifndef USB_MSC_HOST_H
#define USB_MSC_HOST_H

#include "BootTypes.h"

#define USB_MSC_HOST_MAX 4

#define MSC_HOST_XHCI 1
#define MSC_HOST_EHCI 2

typedef struct MSC_HOST {
    int Kind;
    /* 可选；xHCI 用。NULL 则跳过 */
    int (*BringUp)(void);
    int (*Ready)(void);
    int (*Scan)(void);
    int (*Claim)(void);
    int (*Capacity)(void);
    UINT32 (*BlockCount)(void);
    UINT32 (*BlockSize)(void);
    int (*ReadSectors)(UINT32 Lba, UINT32 Count, void *Buffer);
    int (*WriteSectors)(UINT32 Lba, UINT32 Count, const void *Buffer);
    int (*Flush)(void);
    int (*Release)(void);
    int (*Present)(void);
    /* 可选；EHCI 在 Claim/Scan 前问控制器是否已起。NULL=视为已起 */
    int (*ControllerReady)(void);
} MSC_HOST;

void UsbMscHostRegister(const MSC_HOST *Host);
UINTN UsbMscHostCount(void);
const MSC_HOST *UsbMscHostGet(UINTN Index);
const MSC_HOST *UsbMscHostByKind(int Kind);

#endif
