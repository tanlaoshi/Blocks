/*
 * Ehci.h — EHCI 对外 API（PR-H-ehci-1/2/3）
 */
#ifndef EHCI_H
#define EHCI_H

#include "BootTypes.h"
#include "PCIe.h"

int EhciClaim(const USB_CONTROLLER *Pci);
int Setup(void);
int Ready(void);
UINT32 CcsMask(void);
void DiagFormat(char *Buf, int Max);

/* PR-H-ehci-2 */
int HidBringup(void);
int HidReady(void);
void HidPoll(void);
int HidKeyboardDequeue(UINT8 Out[8]);
int HidMousePresent(void);
int HidMouseDequeue(UINT32 *X, UINT32 *Y, UINT8 *Buttons, INT8 *Wheel);

/* PR-H-ehci-3：BOT MSC（UsbMsc 门面分支） */
int MscClaim(void);
int MscReady(void);
int MscScan(void);
int MscCapacity(void);
UINT32 MscBlockCount(void);
UINT32 MscBlockSize(void);
int MscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer);
int MscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer);
int MscFlush(void);
int MscRelease(void);
int MscPresent(void);

/* PR-H-ehci-4：FT232 tee */
int FtdiClaim(void);
void FtdiInvalidate(void);
int FtdiReady(void);
int FtdiWrite(const char *Text); /* 0=ok；<0 失败 */
void FtdiPollRx(void);
int FtdiDataReady(void);
char FtdiReadChar(void);

/* PR-N-wifi-1：RTL8188EU */
int WifiClaim(void);
int WirelessReady(void);
UINT16 WifiPid(void);

#endif
