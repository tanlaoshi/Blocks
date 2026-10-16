/*
 * IwlHwPrivate.h — PCI、MMIO、固件、队列、校准。不含 EAPOL 字段。
 */
#ifndef IWL_HW_PRIVATE_H
#define IWL_HW_PRIVATE_H

#include "IwlState.h"
#include "IwlRegs.h"

typedef struct {
    const UINT8 *Data;
    UINT32 Len;
    UINT32 Offset;
} IWL_FW_SEC;

typedef struct {
    IWL_FW_SEC Sec[IWL_FW_SEC_MAX];
    int NumSec;
    int DualCpus;
    UINT32 PagingMemSize; /* TLV_PAGING；0=无 CPU2 分页镜像 */
} IWL_FW_IMG;

typedef struct {
    UINT8 Code;
    UINT8 Flags;
    UINT8 Idx;
    UINT8 Qid;
} IWL_CMD_HDR;

typedef struct {
    UINT8 Opcode;
    UINT8 GroupId;
    UINT8 Idx;
    UINT8 Qid;
    UINT16 Length;
    UINT8 Reserved;
    UINT8 Version;
} __attribute__((packed)) IWL_CMD_HDR_WIDE;

typedef struct {
    UINT32 LenNFlags;
    IWL_CMD_HDR Hdr;
    UINT8 Data[];
} IWL_RX_PKT;

typedef struct {
    UINT32 Lo;
    UINT16 HiNLen; /* [3:0]=addr[35:32] [15:4]=len */
} __attribute__((packed)) IWL_TFD_TB;

typedef struct {
    UINT8 Reserved[3];
    UINT8 NumTbs;
    IWL_TFD_TB Tb[IWL_TFD_NUM_TBS];
    UINT32 Pad;
} __attribute__((packed)) IWL_TFD;

extern IWL_FW_IMG gIwlImgRt;
extern IWL_FW_IMG gIwlImgInit;
extern UINT8 *gIwlFwBlob;
extern UINTN gIwlFwBlobSize;
extern UINT32 gIwlPhyCfg;
extern UINT32 gIwlCalibFlow[];
extern UINT32 gIwlCalibEvent[];
extern UINT32 gIwlSchedBase;

void IwlRxDataToNet(UINT8 *Frame, UINTN FLen, UINT32 St);
int IwlRxTryDecrypt(UINT8 *Frame, UINTN FLen, UINTN HdrLen, int FwDec,
                    UINTN *BodyOff, UINTN *BodyLen);
void IwlLogDataTx(const IWL_RX_PKT *Pkt, UINTN Len);

UINT32 IwlMmioR32(UINT32 Off);
void IwlMmioW32(UINT32 Off, UINT32 Val);
void IwlMmioSet(UINT32 Off, UINT32 Mask);
void IwlMmioClr(UINT32 Off, UINT32 Mask);
int IwlPollBit(UINT32 Off, UINT32 Bits, UINT32 Mask, UINT32 TimeoutUs);
void IwlStallUs(UINT32 Us);
void IwlStallMs(UINT32 Ms);
void IwlFlushDma(const void *Ptr, UINTN Size);
int IwlNicLock(void);
void IwlNicUnlock(void);
UINT32 IwlPrphR(UINT32 Addr);
void IwlPrphW(UINT32 Addr, UINT32 Val);

int IwlPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut, UINT16 *DidOut);
int IwlMapBar(UINT64 Bar);
void IwlPciPathPrep(void);
int IwlHwStart(void);
int IwlNicInit(void);
int IwlFwTryLoad(void);
int IwlFwParseAndLoad(void);
int IwlLoadUcode8000(const IWL_FW_IMG *Img);
int IwlFhAliveVal(UINT32 V);
void IwlFhProbeAccess(const char *Tag);
void IwlFhLogFail(int Why, UINT32 Dst, UINT32 Phys, UINT32 Tssr, UINT32 Tcsr);
int IwlWaitAlive(void);
void IwlRxDrain(void);
int IwlPagingInit(const IWL_FW_IMG *Img);
int IwlPhyInitCalib(void);
int IwlPhyDbSend(void);
int IwlPhyCfgRt(void);

int IwlRxInit(void);
void IwlRxPoll(void);
int IwlRxTake(IWL_RX_PKT **OutPkt, UINTN *OutLen);
void IwlRxHoldMpdu(const IWL_RX_PKT *Pkt, UINTN Len);
int IwlRxTakeHeld(IWL_RX_PKT **OutPkt, UINTN *OutLen);
UINT8 IwlRxHoldCount(void);
void IwlRxHoldFlush(void);
void IwlRxRestock(void);
UINT32 IwlRxDiagClosed(void);
UINT32 IwlRxDiagRead(void);

int IwlTxInit(void);
int IwlSendCmd(UINT32 Id, const void *Data, UINT32 Len, int Sync);
void IwlCmdKick(void);
int IwlCmdqUnwedge(void);
int IwlSendFrameRaw(const UINT8 *Frame80211, UINTN Len);
int IwlRspStashClaim(UINT8 Code, UINT8 Idx);
void IwlCmdqSnap(void);
int IwlPostAlive(void);
int IwlEnableAuxTxq(void);
int IwlPrepareApTxq(void);
int IwlEnableApTxq(void);
void IwlLogApQ(void);
int IwlCmdqReset(void);
void IwlAuxTxLogReset(void);

int IwlAddApSta(void);
int IwlPhyCtxtTune(UINT8 Chan);
void IwlProtectSession(void);
void IwlReadHwMac(void);
void IwlApplyHwMac(void);
int IwlMacCtxtPrep(void);
int IwlMacCtxtAssoc(void);
int IwlStaKeysInstall(void);
int IwlMvmPostAlive(void);

#endif /* IWL_HW_PRIVATE_H */
