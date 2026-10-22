/*
 * Private.h — EHCI 寄存器 / QH·qTD（PR-H-ehci-1/2）
 *
 * 对照 EHCI 1.0；不搬 Linux ehci-hcd。
 * qTD/QH 用 64-bit 寻址布局（BufHi），Intel HCCPARAMS.AC 常为 1。
 */
#ifndef EHCI_PRIVATE_H
#define EHCI_PRIVATE_H

#include "Compiler.h"

#include "Ehci.h"
#include "PCIe.h"
#include "XHCI.h" /* USB_SETUP_PACKET / DEVICE_DESCRIPTOR */

#define EHCI_PROG_IF           0x20u

#define EHCI_CAPLENGTH         0x00u
#define EHCI_HCSPARAMS         0x04u
#define EHCI_HCCPARAMS         0x08u
#define EHCI_HCS_N_PORTS(V)    ((V) & 0x0Fu)
#define EHCI_HCC_EECP(V)       (((V) >> 8) & 0xFFu)
#define EHCI_HCC_64ADDR(V)     ((V) & 1u)

#define EHCI_USBCMD            0x00u
#define EHCI_USBSTS            0x04u
#define EHCI_FRINDEX           0x0Cu
#define EHCI_CTRLDSSEGMENT     0x10u
#define EHCI_PERIODICLISTBASE  0x14u
#define EHCI_ASYNCLISTADDR     0x18u
#define EHCI_CONFIGFLAG        0x40u
#define EHCI_PORTSC(N)         (0x44u + 4u * ((N) - 1u))

#define EHCI_CMD_RS            (1u << 0)
#define EHCI_CMD_HCRESET       (1u << 1)
#define EHCI_CMD_PSE           (1u << 4)
#define EHCI_CMD_ASE           (1u << 5)
#define EHCI_CMD_IAAD          (1u << 6)

#define EHCI_STS_USBINT        (1u << 0)
#define EHCI_STS_USBERR        (1u << 1)
#define EHCI_STS_PCD           (1u << 2)
#define EHCI_STS_FLR           (1u << 3)
#define EHCI_STS_IAA           (1u << 5)
#define EHCI_STS_HCHALTED      (1u << 12)
#define EHCI_STS_PSS           (1u << 14)
#define EHCI_STS_ASS           (1u << 15)

#define EHCI_PORT_CCS          (1u << 0)
#define EHCI_PORT_CSC          (1u << 1)
#define EHCI_PORT_PED          (1u << 2)
#define EHCI_PORT_PEDC         (1u << 3)
#define EHCI_PORT_OCC          (1u << 5)
#define EHCI_PORT_PR           (1u << 8)
#define EHCI_PORT_LS           (1u << 9)
#define EHCI_PORT_PP           (1u << 12)
#define EHCI_PORT_OWNER        (1u << 13)

#define EHCI_LEGSUP_BIOS       (1u << 16)
#define EHCI_LEGSUP_OS         (1u << 24)

#define EHCI_MAX_CTRL          4
#define EHCI_BAR_MAP           0x1000u
#define EHCI_FRAME_ENTRIES     1024u

#define EHCI_LINK_TERMINATE    1u
#define EHCI_LINK_TYPE_QH      (1u << 1)

#define EHCI_QTD_PID_OUT       0u
#define EHCI_QTD_PID_IN        1u
#define EHCI_QTD_PID_SETUP     2u
#define EHCI_QTD_ACTIVE        (1u << 7)
#define EHCI_QTD_HALTED        (1u << 6)
#define EHCI_QTD_DT            (1u << 31)

#define EHCI_SPEED_FS          0u
#define EHCI_SPEED_LS          1u
#define EHCI_SPEED_HS          2u

/* 64-bit 寻址格式（Appendix B）；BufHi 置 0，DMA 须 <4G */
typedef struct {
    UINT32 Next;
    UINT32 AltNext;
    UINT32 Token;
    UINT32 Buf[5];
    UINT32 BufHi[5];
    UINT32 Pad[3]; /* → 64B，避免相邻 qTD 被 HC 当同一描述符读穿 */
} BLOCKS_PACKED BLOCKS_ALIGNED(64) EHCI_QTD;

typedef struct {
    UINT32 Horiz;
    UINT32 EpChar;
    UINT32 EpCap;
    UINT32 Current;
    /* overlay */
    UINT32 Next;
    UINT32 AltNext;
    UINT32 Token;
    UINT32 Buf[5];
    UINT32 BufHi[5];
    UINT32 Pad; /* → 68B spec；再 pad 到 128B 槽 */
    UINT32 Pad2[14];
} BLOCKS_PACKED BLOCKS_ALIGNED(64) EHCI_QH;

typedef struct {
    USB_CONTROLLER Pci;
    volatile UINT8 *Cap;
    volatile UINT8 *Op;
    UINT8 CapLen;
    UINT8 NPorts;
    UINT32 CcsMask;
    int Up;
    int Sched;
    UINT8 *Dma;
    UINT64 DmaPhys;
    UINT32 *FrameList;
    EHCI_QH *AsyncHead;
    EHCI_QH *CtrlQh;
    EHCI_QH *IntrQh;
    EHCI_QTD *Qtds;
    UINT8 *CtrlBuf;
    UINT8 *SetupBuf;
    UINT8 *ReportBuf;
    UINT8 *BulkBuf; /* PR-H-ehci-3：BOT bounce（512） */
    /* 当前 control 路由（根口 HS：Hub=0；RMH 后设备填 Hub/Speed） */
    UINT8 XferSpeed; /* 0=FS 1=LS 2=HS */
    UINT8 XferHubAddr;
    UINT8 XferHubPort;
    UINT8 DevAddr;
    UINT8 DevSpeed;
    UINT8 HubAddr;
    UINT8 HubPort;
    UINT8 HidIface;
    UINT8 HidEp;
    UINT8 HidProto; /* 1=kbd 2=mouse */
    UINT16 HidMaxPkt;
    UINT8 HidInterval;
    int HidOk;
    /* PR-H-ehci-3 MSC（与 HID 分字段，勿覆盖 HubPort） */
    UINT8 MscAddr;
    UINT8 MscSpeed;
    UINT8 MscHubAddr;
    UINT8 MscHubPort;
    UINT8 MscIface;
    UINT8 MscEpIn;  /* 含 DIR 位 */
    UINT8 MscEpOut;
    UINT16 MscMpsIn;
    UINT16 MscMpsOut;
    UINT8 MscDtIn;
    UINT8 MscDtOut;
    UINT8 MscEpMax0;
    int MscOk;
    int MscCapacityOk;
    UINT32 MscBlockCount;
    UINT32 MscBlockSize;
} EHCI_CTRL;

extern EHCI_CTRL gEhci[EHCI_MAX_CTRL];
extern int gEhciCount;
extern int gEhciReady;
extern UINT32 gEhciCcsOr;
extern int gEhciHidReady;
extern UINT8 gEhciNextAddr;
extern const char *gEhciLastErr;
extern char gEhciHubNote[96];

static inline UINT32 R32(volatile UINT8 *Base, UINT32 Off) {
    return *(volatile UINT32 *)(Base + Off);
}

static inline void W32(volatile UINT8 *Base, UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(Base + Off) = Val;
}

static inline UINT64 PtrPhys(const void *P) {
    return (UINT64)(UINTN)P;
}

static inline void Fence(void) {
    __asm__ __volatile__("mfence" ::: "memory");
}

void Delay(int Loops);
void Flush(const void *Ptr, UINTN Size);

int InitOne(EHCI_CTRL *C, int Index);
void SurveyCcs(EHCI_CTRL *C);
int SchedStart(EHCI_CTRL *C);
void SchedEnablePeriodic(EHCI_CTRL *C);
int ControlTransfer(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax,
                    const USB_SETUP_PACKET *Setup, void *Data);
/* DirIn=1 Bulk IN；成功 0 */
int BulkXfer(EHCI_CTRL *C, UINT8 Addr, UINT8 Ep, UINT16 MaxPkt,
                 UINT8 Speed, UINT8 HubAddr, UINT8 HubPort, int DirIn,
                 void *Buf, UINT32 Len, UINT8 *Dt);
int PortReset(EHCI_CTRL *C, UINT8 Port);

int EnumGetDesc(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT16 TypeIndex,
                    UINT16 Len, void *Out);
int EnumSetAddr(EHCI_CTRL *C, UINT8 NewAddr, UINT8 EpMax);
int EnumSetConfig(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT8 Cfg);
int EnumSetProtocolBoot(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT8 Iface);
int EnumSetIdle(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT8 Iface);
void EnumMarkHex4(const char *Prefix, UINT32 V);

int EnumDevice(EHCI_CTRL *C, UINT8 Speed, UINT8 HubAddr, UINT8 HubPort);
int EnumHub(EHCI_CTRL *C);
int EnumHid(EHCI_CTRL *C);
void HidArmIntr(EHCI_CTRL *C);
extern UINT8 gEhciIntrDt;
void HidResetQueues(void);
int HidBringup(void);
void HidPoll(void);
int HidKeyboardDequeue(UINT8 Out[8]);
int HidMousePresent(void);
int HidMouseDequeue(UINT32 *X, UINT32 *Y, UINT8 *Buttons, INT8 *Wheel);

/* PR-H-ehci-3 */
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

/* async helpers（control/bulk 共用） */
void CopyBuf(UINT8 *D, const UINT8 *S, UINT32 N);
void PrepQtd(EHCI_QTD *T, UINT32 Next, UINT32 Pid, UINT32 Bytes,
                 UINT32 Dt, UINT64 BufPhys);
int AsyncOff(EHCI_CTRL *C);
int AsyncOn(EHCI_CTRL *C);
int WaitQtd(EHCI_CTRL *C, EHCI_QTD *T);

/* MSC 跨文件态 / claim·bot 帮手 */
extern EHCI_CTRL *gEhciMscCtrl;
extern int gEhciMscInSense;
extern UINT32 gEhciMscTag;
#define EHCI_HUB_STAT_CONNECT (1u << 0)
void MscZero(UINT8 *P, UINT32 N);
void MscCopy(UINT8 *D, const UINT8 *S, UINT32 N);
void MscMarkHex4(const char *Prefix, UINT32 V);
int MscFinishClaim(EHCI_CTRL *C, UINT8 Speed, UINT8 HubAddr, UINT8 HubPort);
int MscClaimViaHub(EHCI_CTRL *C);
int MscEnsureHub(EHCI_CTRL *C, UINT8 Port);
int MscBot(EHCI_CTRL *C, UINT8 *CbwCb, UINT8 CbLen, UINT32 DataLen,
               int DataIn, void *Data);
int MscHubGetStatus(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port,
                        UINT16 *Status, UINT16 *Change);
int MscHubSetFeat(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT16 Feat);
int MscHubClearFeat(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT16 Feat);
int MscHubGetDesc(EHCI_CTRL *C, UINT8 HubAddr, UINT8 *Out, UINT16 Len);
int MscHubResetPort(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT8 *SpeedOut);

/* PR-H-ehci-4 FTDI */
extern EHCI_CTRL *gEhciFtdiCtrl;
extern UINT8 gEhciFtdiAddr;
extern UINT8 gEhciFtdiSpeed;
extern UINT8 gEhciFtdiHubAddr;
extern UINT8 gEhciFtdiHubPort;
extern UINT8 gEhciFtdiEpIn;
extern UINT8 gEhciFtdiEpOut;
extern UINT16 gEhciFtdiMpsIn;
extern UINT16 gEhciFtdiMpsOut;
extern UINT8 gEhciFtdiDtIn;
extern UINT8 gEhciFtdiDtOut;
extern UINT8 gEhciFtdiEpMax0;
extern int gEhciFtdiOk;
int FtdiClaimViaHub(EHCI_CTRL *C);
void FtdiInvalidate(void);
int FtdiClaim(void);
int FtdiReady(void);
int FtdiWrite(const char *Text);
void FtdiPollRx(void);
int FtdiDataReady(void);
char FtdiReadChar(void);

/* PR-N-wifi-1 */
int WifiClaimViaHub(EHCI_CTRL *C);
int WifiClaim(void);
int WirelessReady(void);
UINT16 WifiPid(void);

#endif
