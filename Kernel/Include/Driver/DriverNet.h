/*
 * DriverNet.h — Net 类适配（PR-D3）
 *
 * 驱动 Bind 时调用 DriverNetAttach；Common 仍只见 HalNet*。
 */
#ifndef DRIVER_NET_H
#define DRIVER_NET_H

#include "BootTypes.h"

typedef struct {
    int (*Ready)(void);
    void (*Poll)(void);
    void (*GetMac)(UINT8 Mac[6]);
    UINT32 (*GetIp)(void);
    void (*SetIp)(UINT32 Ip);
    void (*FormatIp)(UINT32 Ip, char *Buf, int BufLen);
    int (*ParseIp)(const char *Text, UINT32 *Ip);
    int (*Ping)(const char *Host, int TimeoutMs);
    void (*GetStats)(UINT32 *TxDone, UINT32 *RxFrames);
    int (*SendIp)(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
    UINT16 (*Checksum)(const void *Data, UINTN Len);
    void (*SetLwIpRx)(int Enable);
} NET_BACKEND;

int DriverNetAttach(const NET_BACKEND *Backend);
int DriverNetReady(void);

void DriverNetPoll(void);
void DriverNetGetMac(UINT8 Mac[6]);
UINT32 DriverNetGetIp(void);
void DriverNetSetIp(UINT32 Ip);
void DriverNetFormatIp(UINT32 Ip, char *Buf, int BufLen);
int DriverNetParseIp(const char *Text, UINT32 *Ip);
int DriverNetPing(const char *Host, int TimeoutMs);
void DriverNetGetStats(UINT32 *TxDone, UINT32 *RxFrames);
int DriverNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
UINT16 DriverNetChecksum(const void *Data, UINTN Len);
void DriverNetSetLwIpRx(int Enable);

#endif
