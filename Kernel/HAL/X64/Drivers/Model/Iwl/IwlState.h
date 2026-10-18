/*
 * IwlState.h — Hw 与 Mac 都要看的共享状态。
 * 不含 MMIO 声明，不含 EAPOL 状态机字段。
 */
#ifndef IWL_STATE_H
#define IWL_STATE_H

#include "Iwl.h"

#define IWL_FW_PATH   "FW/IWL8265.UCODE"
#define IWL_CFG_PATH  "FW/WIFI.CFG"
#define IWL_FW_SEC_MAX 16
#define IWL_SSID_MAX   32
#define IWL_PSK_MAX    63

extern volatile UINT8 *gIwlBar;
extern UINT64 gIwlBarPhys;
extern UINT64 gIwlBarSize;
extern UINT16 gIwlDid;
extern UINT16 gIwlHwRev;
extern UINT8 gIwlBus;
extern UINT8 gIwlDev;
extern UINT8 gIwlFn;
extern UINT8 gIwlMac[6];
extern UINT8 gIwlBssid[6];
extern UINT16 gIwlAid;
extern int gIwlReady;
extern int gIwlFwOk;
extern int gIwlBarOk;
extern int gIwlAlive;
extern int gIwlAssociated;
extern int gIwlWpa2Ok;
extern int gIwlScanCount;
extern int gIwlSsidOk;
extern UINTN gIwlFwSize;
extern char gIwlSsid[IWL_SSID_MAX + 1];
extern char gIwlPsk[IWL_PSK_MAX + 1];
extern UINT8 gIwlPmk[32];
extern int gIwlPmkOk;
extern UINT8 gIwlTxStaId;
extern int gDatTxLogged;
extern int gDatRxLogged;
extern int gDtxLogged;
extern int gRxMicLogged;
extern int gRxMicU;
extern int gRxDiscLogged;
extern int gRxOffLogged;
extern int gRxLlcLogged;
extern int gRxStLogged;

#ifndef IWL_LOG_VERBOSE
#define IWL_LOG_VERBOSE 0 /* 1=成功里程碑/FH/rxraw/cmdq 等全开黄字 */
#endif

void IwlLogBound(void);
void IwlLogStage(const char *Tag);
void IwlLogVerb(const char *Tag);

#endif /* IWL_STATE_H */
