/*
 * Sdhci/Private.h — Duo SDHCI 内部（bd-duo-sd）
 */
#ifndef BOARD_SDHCI_PRIVATE_H
#define BOARD_SDHCI_PRIVATE_H

#include "../Sdhci.h"
#include "Board.h"
#include "Block.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Hal.h"
#include "HalSerial.h"

#define IDENTIFY_HERTZ        400000u
#define TRANSFER_HERTZ      25000000u
/* cv1800b_sdhci.c：CAP 基钟坏掉，分频按这个上限 */
#define SD_BASE_CLOCK_HERTZ 375000000u
#define AREA1_MASK 0xFFFu

extern UINT16 gSdhciHostSpecification;
extern UINT32 gSdhciVendorArea;
extern UINT32 gSdhciRelativeCardAddress;
extern UINT32 gSdhciResponse[4];
extern UINT32 gSdhciLastStatus;
extern UINT32 gSdhciLastPresentState;
extern int gSdhciHighCapacity;
extern int gSdhciReady;

volatile UINT32 *SdhciRegister32(UINT32 Offset);
UINT64 SdhciReadTime(void);
void SdhciDelayMicroseconds(UINT32 Microseconds);
UINT32 SdhciRead32(UINT32 Offset);
UINT8 SdhciRead8(UINT32 Offset);
UINT16 SdhciRead16(UINT32 Offset);
void SdhciWrite32(UINT32 Offset, UINT32 Value);
void SdhciWrite8(UINT32 Offset, UINT8 Value);
void SdhciWrite16(UINT32 Offset, UINT16 Value);
void SdhciMark(const char *Text);
void SdhciMarkHexadecimal32(const char *Tag, UINT32 Value);
int SdhciWaitMilliseconds8(UINT32 Offset, UINT8 Mask, int Set, UINT32 Milliseconds);
int SdhciWaitMilliseconds32(UINT32 Offset, UINT32 Mask, int Set, UINT32 Milliseconds);
int SdhciSoftwareReset(UINT8 Mask);
int SdhciPhysicalAfterReset(void);
int SdhciClockConfigure(UINT32 Hertz);
int SdhciCommandSend(UINT8 CommandIndex, UINT32 Argument, UINT16 Flags, UINT16 TransferMode);
int SdhciProgrammedIoRead(UINT8 *Buffer);
int SdhciProgrammedIoWrite(const UINT8 *Buffer);
int SdhciReadOneBlock(UINT32 LogicalBlockAddress, UINT8 *Buffer);
int SdhciWriteOneBlock(UINT32 LogicalBlockAddress, const UINT8 *Buffer);
void SdhciFail(const char *What);
int SdhciCardInitialize(void);
int SdhciHostInitialize(void);

#endif
