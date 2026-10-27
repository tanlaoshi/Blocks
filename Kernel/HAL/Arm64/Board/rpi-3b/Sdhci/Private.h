/*
 * Sdhci/Private.h — Pi3B Sdhci 内部（bd-pi-sd）
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
#include "SerialLog.h"

#define IDENTIFY_HERTZ        400000u
#define TRANSFER_HERTZ      25000000u
#define SPIN_MAXIMUM     2000000u
#define WAIT_MILLISECONDS      100u

extern UINT16 gSdhciHostSpecification;
extern UINT32 gSdhciLastCommandStatus;
extern UINT32 gSdhciLastPresentState;
extern UINT64 gSdhciCounterFrequency;
extern UINT64 gSdhciLastWriteMicroseconds;
extern UINT32 gSdhciTwoTicksMicroseconds;
extern UINT32 gSdhciBaseClockHertz;
extern UINT32 gSdhciRelativeCardAddress;
extern int gSdhciHighCapacity;
extern int gSdhciReady;
extern UINT32 gSdhciResponse[4];



volatile UINT32 *SdhciRegister32(UINT32 Offset);
UINT64 SdhciCounterNow(void);
void SdhciDelayMicroseconds(UINT32 Microseconds);
UINT64 SdhciNowMicroseconds(void);
void SdhciWrite32(UINT32 Offset, UINT32 Value);
UINT32 SdhciRead32(UINT32 Offset);
UINT8 SdhciRead8(UINT32 Offset);
UINT16 SdhciRead16(UINT32 Offset);
void SdhciWrite8(UINT32 Offset, UINT8 Value);
void SdhciWrite16(UINT32 Offset, UINT16 Value);
int SdhciWaitMillisecondsMask32(UINT32 Offset, UINT32 Mask, int Set, UINT32 Milliseconds);
int SdhciWaitMillisecondsMask8(UINT32 Offset, UINT8 Mask, int Set, UINT32 Milliseconds);
void SdhciWrite16Pair(UINT32 OffsetLow, UINT16 LowHalf, UINT16 HighHalf);
int SdhciSoftwareReset(UINT8 Mask);
int SdhciClockConfigure(UINT32 Hertz);
int SdhciCommandSend(UINT8 CommandIndex, UINT32 Argument, UINT16 Flags, UINT16 TransferMode);
int SdhciProgrammedIoReadBlock(UINT8 *Buffer);
int SdhciReadOneBlock(UINT32 LogicalBlockAddress, UINT8 *Buffer);
void SdhciMarkHexadecimal32(const char *Tag, UINT32 Value);
int SdhciCardInitialize(void);
int SdhciHostPrepare(void); /* 电源/钟/版本；不含 SD 卡识别（WiFi SDIO 用） */
int SdhciHostInitialize(void);


#endif
