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

int Ping(const char *Host, int TimeoutMs);

void UdpInitialize(void);
int UdpBind(UINT16 Port);
UINT16 UdpBoundPort(void);
int UdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len);
int UdpRecv(NETWORK_UDP_DG *Out);
void UdpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len);
void UdpInputFrame(const UINT8 *Frame, int Len);
/* 短轮询收帧入 UDP 队列；有新报返回 1 */
int UdpPoll(int TimeoutMs);

void TcpInitialize(void);
int TcpListen(UINT16 Port);
int TcpConnect(UINT32 DstIp, UINT16 DstPort);
int TcpSend(const void *Data, UINTN Len);
void TcpClose(void);
NETWORK_TCP_STATE TcpGetState(void);
void TcpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len);
void TcpInputFrame(const UINT8 *Frame, int Len);
void TcpPoll(int TimeoutMs);

#endif
