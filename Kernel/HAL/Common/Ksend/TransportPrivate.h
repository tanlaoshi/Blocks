/*
 * TransportPrivate.h — Transport.c / TransportReceive.c 共享状态
 * 不对外；仅 Ksend 传输实现互调。
 */
#pragma once

#include "Ksend/Private.h"

/* 收窗加大：有线 win=16 单窗 64KiB；应用缓冲 8KiB，勿回退 512 碎 Recv */
#define TRANSPORT_RECEIVE_BYTES 8192u
#define TRANSPORT_SEND_BYTES    512u
#define TRANSPORT_SEND_RETRY_MILLISECONDS 15000u
#define TRANSPORT_DRAIN_EMPTY_MILLISECONDS 30000u
#define TRANSPORT_CTRL_C 0x03u

extern int gTransportSocket;
extern int gTransportSendBroken;
extern int gTransportUserAbort;
extern UINT8 gReceiveBuffer[TRANSPORT_RECEIVE_BYTES];
extern UINT32 gReceiveLength;
extern UINT32 gReceivePosition;
extern UINT8 gSendBuffer[TRANSPORT_SEND_BYTES];
extern UINT32 gSendLength;

int TransportTakeCtrlC(UINT8 Value);
int TransportPollUserAbort(void);
int TransportElapsed(UINT32 Start, UINT32 LimitMilliseconds);
void TransportSendFlush(UINT32 RetryMilliseconds);
int TransportSendFlushUntilEmpty(UINT32 LimitMilliseconds);
