/*
 * Network.h — 网络模块
 *
 * PCI 探卡 + HalNet；K38 ping；K39 UDP；K40 TCP；lwIP 另刀。
 */
#ifndef NETWORK_H
#define NETWORK_H

#include "BootTypes.h"

#define NETWORK_UDP_PAYLOAD_MAX 512u

typedef struct {
    UINT32 SrcIp;
    UINT16 SrcPort;
    UINT16 DstPort;
    UINT16 Len;
    UINT8  Data[NETWORK_UDP_PAYLOAD_MAX];
} NETWORK_UDP_DG;

typedef enum {
    NETWORK_TCP_CLOSED = 0,
    NETWORK_TCP_LISTEN,
    NETWORK_TCP_SYN_SENT,
    NETWORK_TCP_SYN_RCVD,
    NETWORK_TCP_ESTABLISHED,
} NETWORK_TCP_STATE;

int NetworkInitialize(void);
int NetworkNicReady(void);
UINT16 NetworkNicVendorId(void);
UINT16 NetworkNicDeviceId(void);

UINT32 NetworkSelfIp(void);
UINT32 NetworkGwIp(void);
int NetworkParseIp(const char *S, UINT32 *Out);
void NetworkArpLearn(UINT32 Ip, const UINT8 Mac[6]);
int NetworkArpResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs);
int NetworkSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN Len);
void NetworkEatArpFromFrame(const UINT8 *Buf, int Len);

int NetworkPing(const char *Host, int TimeoutMs);

void NetworkUdpInitialize(void);
int NetworkUdpBind(UINT16 Port);
UINT16 NetworkUdpBoundPort(void);
int NetworkUdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len);
int NetworkUdpRecv(NETWORK_UDP_DG *Out);
void NetworkUdpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len);
void NetworkUdpInputFrame(const UINT8 *Frame, int Len);
/* 短轮询收帧入 UDP 队列；有新报返回 1 */
int NetworkUdpPoll(int TimeoutMs);

void NetworkTcpInitialize(void);
int NetworkTcpListen(UINT16 Port);
int NetworkTcpConnect(UINT32 DstIp, UINT16 DstPort);
int NetworkTcpSend(const void *Data, UINTN Len);
void NetworkTcpClose(void);
NETWORK_TCP_STATE NetworkTcpGetState(void);
void NetworkTcpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len);
void NetworkTcpInputFrame(const UINT8 *Frame, int Len);
void NetworkTcpPoll(int TimeoutMs);

#endif
