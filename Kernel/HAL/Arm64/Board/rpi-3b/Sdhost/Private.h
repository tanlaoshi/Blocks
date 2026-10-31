/*
 * Sdhost/Private.h — Pi3B Sdhost 内部（bd-pi-sd）
 */
#ifndef BOARD_SDHOST_PRIVATE_H
#define BOARD_SDHOST_PRIVATE_H

#include "../Sdhost.h"
#include "Board.h"
#include "Block.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Hal.h"
#include "HalSerial.h"
#include "SerialLog.h"

#define IDENTIFY_HERTZ   400000u
#define TRANSFER_HERTZ 25000000u

#define RESPONSE_NONE 0
#define RESPONSE_R1   1
#define RESPONSE_R1B  2
#define RESPONSE_R2   3
#define RESPONSE_R3   4 /* OCR：允许 CRC7 FAIL（对齐 U-Boot SEND_OP_COND） */

extern UINT32 gSdhostMaximumClock;
extern UINT32 gSdhostHostConfiguration;
extern UINT32 gSdhostClockDivider;
extern UINT32 gSdhostRelativeCardAddress;
extern UINT32 gSdhostResponse[4];
extern int gSdhostHighCapacity;
extern int gSdhostReady;
extern UINT32 gSdhostLastCommand;
extern UINT32 gSdhostLastHostStatus;
extern UINT64 gSdhostCounterFrequency;



volatile UINT32 *SdhostRegister32(UINT32 Offset);
void SdhostWrite32(UINT32 Offset, UINT32 Value);
UINT32 SdhostRead32(UINT32 Offset);
UINT64 SdhostCounterNow(void);
void SdhostDelayMicroseconds(UINT32 Microseconds);
void SdhostDelayMilliseconds(UINT32 Milliseconds);
void SdhostMarkHexadecimal32(const char *Tag, UINT32 Value);
void SdhostDumpRegisters(const char *Reason);
void SdhostResetController(void);
void SdhostClockConfigure(UINT32 Clock);
void SdhostInterfaceConfigure(UINT32 ClockHertz, int WideFourBit);
UINT32 SdhostWaitCommandRegisterClear(UINT32 TimeoutMicroseconds);
int SdhostWaitTransferComplete(void);
int SdhostTransferBlockProgrammedIoRead(UINT8 *Buffer);
int SdhostTransferBlockProgrammedIoWrite(const UINT8 *Buffer);
/* DataBuffer!=0 时：IsWrite=0 读（CMD17），IsWrite=1 写（CMD24） */
int SdhostCommandSend(UINT8 CommandIndex, UINT32 Argument, int ResponseType, UINT8 *DataBuffer,
                      int IsWrite);
int SdhostReadOneBlock(UINT32 LogicalBlockAddress, UINT8 *Buffer);
int SdhostWriteOneBlock(UINT32 LogicalBlockAddress, const UINT8 *Buffer);
int SdhostCardInitialize(void);
int SdhostHostInitialize(void);


#endif
