/*
 * IwlMacPrivate.h — 扫描、关联、EAPOL。不含 MMIO。
 */
#ifndef IWL_MAC_PRIVATE_H
#define IWL_MAC_PRIVATE_H

#include "IwlState.h"

typedef struct {
    UINT8 Bssid[6];
    UINT8 Chan;
    INT8 Rssi;
    UINT16 Caps;
    UINT8 SsidLen;
    UINT8 Ssid[IWL_SSID_MAX];
    UINT8 HasRsn;
    UINT8 RatesLen;
    UINT8 Rates[8];
    UINT8 ExtRatesLen;
    UINT8 ExtRates[8];
    UINT8 HtLen;
    UINT8 Ht[26];
    UINT8 RsnLen;
    UINT8 Rsn[48];
} IWL_BSS;

extern UINT8 gIwlPtk[16];
extern UINT8 gIwlGtk[32]; /* CCMP=16；TKIP TK=32 */
extern UINT8 gIwlGtkAlt[16];
extern UINT8 gIwlGtkId;
extern UINT8 gIwlGtkAltOk;
extern UINT8 gIwlGtkLen;
extern UINT8 gIwlGroupCipher; /* RSN suite 末字节：2=TKIP 4=CCMP */
extern UINT8 gIwlStaRsn[32];
extern UINT8 gIwlStaRsnLen;
extern IWL_BSS gIwlTarget;
extern int gIwlTxRsp;
extern int gIwlRxCode;

int IwlCfgLoad(void);
int IwlPmkPrepare(void);
int IwlScanRun(void);
int IwlAssocRun(void);
int IwlEapolRun(void);

void IwlEapolZero(void *P, UINTN N);
void IwlEapolCopyN(UINT8 *D, const UINT8 *S, UINTN N);
int IwlEapolMemCmp(const UINT8 *A, const UINT8 *B, UINTN N);
UINTN IwlDot11DataHdrLen(UINT16 Fc);
int IwlFindEapol(UINT8 *Dot11, UINTN FLen, UINT8 **OutEap, UINTN *OutLen);
int IwlAddr1IsUs(const UINT8 *Dot11);
UINT16 IwlBe16(const UINT8 *P);
void IwlPutBe16(UINT8 *P, UINT16 V);
void IwlPrf384(const UINT8 Pmk[32], const UINT8 *A, UINTN Alen,
               const UINT8 *B, UINTN Blen, UINT8 Out[48]);
void IwlBuildPtk(const UINT8 Pmk[32], const UINT8 *Anon, const UINT8 *Snon,
                 UINT8 Ptk[48], UINT8 Ver);
void IwlEapolMic(UINT8 Ver, const UINT8 Kck[16], UINT8 *Eapol,
                 UINTN EapolLen, UINT8 Mic[16]);
void IwlInstallGtk(const UINT8 *Eapol, UINTN EapLen, const UINT8 Kek[16]);
int IwlRxDrainForEapol(UINT8 *EapOut, UINTN *EapLenOut, UINTN Cap,
                       UINT32 *MpduN, UINT32 *DataN, UINT32 *UniN,
                       UINT32 *BeaconN);
int IwlSendEapol(const UINT8 *Eapol, UINTN EapolLen);
int IwlEapolWaitMsg1(UINT8 *Eapol, UINTN *EapLen, UINT8 *Anonce, UINT8 *Replay,
                     UINT8 *KeyDescOut);
int IwlEapolFinishHandshake(UINT8 *Eapol, UINTN EapLen, UINT8 *Anonce, UINT8 *Replay,
                            UINT8 KeyDesc);

#endif /* IWL_MAC_PRIVATE_H */
