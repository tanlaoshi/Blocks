/*
 * AttNotifyQueue.c — ATT Handle Value Notification 专用环（与应答队列分流）
 * 分流避免 Notify 洪泛堵死 AttExpect；深度有限、满则丢最旧。
 */
#include "XHCI/Bluetooth/Private.h"
#define ATT_NOTIFY_QUEUE_DEPTH 16u
#define ATT_NOTIFY_QUEUE_MAX 32u
#define L2CAP_CID_ATTRIBUTE 0x0004u
#define ATT_HANDLE_VALUE_NOTIFICATION 0x1Bu
#define ATT_HANDLE_VALUE_INDICATION 0x1Du
static UINT8 sNotify[ATT_NOTIFY_QUEUE_DEPTH][ATT_NOTIFY_QUEUE_MAX];
static UINT16 sNotifyLength[ATT_NOTIFY_QUEUE_DEPTH];
static UINT32 sNotifyHead;
static UINT32 sNotifyTail;
static UINT32 sNotifyCount;
static UINT32 sNotifyDropped;
static int sNotifyAccept;
void BluetoothAttNotifyAcceptSet(int Accept) {
    sNotifyAccept = Accept ? 1 : 0;
    if (!sNotifyAccept) {
        BluetoothAttNotifyQueueFlush();
    }
}
int BluetoothAttNotifyAcceptGet(void) {
    return sNotifyAccept;
}
void BluetoothAttNotifyQueueFlush(void) {
    sNotifyHead = 0;
    sNotifyTail = 0;
    sNotifyCount = 0;
}
UINT32 BluetoothAttNotifyDroppedGet(void) {
    return sNotifyDropped;
}
int BluetoothAttNotifyQueueIsAttributeNotification(const UINT8 *AclPayload,
                                                   UINT16 Length) {
    UINT16 L2Length;
    UINT16 ChannelIdentifier;
    UINT8 Opcode;
    if (AclPayload == 0 || Length < 5u) {
        return 0;
    }
    L2Length = (UINT16)(AclPayload[0] | ((UINT16)AclPayload[1] << 8));
    ChannelIdentifier =
        (UINT16)(AclPayload[2] | ((UINT16)AclPayload[3] << 8));
    if (ChannelIdentifier != L2CAP_CID_ATTRIBUTE || L2Length == 0u ||
        (UINT16)(4u + L2Length) > Length) {
        return 0;
    }
    Opcode = AclPayload[4];
    return (Opcode == ATT_HANDLE_VALUE_NOTIFICATION ||
            Opcode == ATT_HANDLE_VALUE_INDICATION)
               ? 1
               : 0;
}
void BluetoothAttNotifyQueuePushAcl(const UINT8 *AclPayload, UINT16 Length) {
    UINT16 L2Length;
    UINT16 Index;
    UINT16 StoreLength;
    if (!BluetoothAttNotifyQueueIsAttributeNotification(AclPayload, Length)) {
        return;
    }
    /* 未在订窗口：丢弃 Notify，避免 ACL 洪泛占满队列拖死有线鼠 */
    if (!sNotifyAccept) {
        sNotifyDropped++;
        return;
    }
    L2Length = (UINT16)(AclPayload[0] | ((UINT16)AclPayload[1] << 8));
    StoreLength = L2Length;
    if (StoreLength > ATT_NOTIFY_QUEUE_MAX) {
        StoreLength = ATT_NOTIFY_QUEUE_MAX;
    }
    if (sNotifyCount >= ATT_NOTIFY_QUEUE_DEPTH) {
        /* 丢最旧，保最新报告 */
        sNotifyHead = (sNotifyHead + 1u) % ATT_NOTIFY_QUEUE_DEPTH;
        sNotifyCount--;
        sNotifyDropped++;
    }
    for (Index = 0; Index < StoreLength; Index++) {
        sNotify[sNotifyTail][Index] = AclPayload[4u + Index];
    }
    sNotifyLength[sNotifyTail] = StoreLength;
    sNotifyTail = (sNotifyTail + 1u) % ATT_NOTIFY_QUEUE_DEPTH;
    sNotifyCount++;
}
int BluetoothAttNotifyQueueTake(UINT8 *AttributeProtocolDataUnitOut,
                                UINT16 Capacity, UINT16 *LengthOut) {
    UINT16 Length;
    UINT16 Index;
    if (AttributeProtocolDataUnitOut == 0 || Capacity == 0u ||
        sNotifyCount == 0u) {
        return 0;
    }
    Length = sNotifyLength[sNotifyHead];
    if (Length > Capacity) {
        Length = Capacity;
    }
    for (Index = 0; Index < Length; Index++) {
        AttributeProtocolDataUnitOut[Index] = sNotify[sNotifyHead][Index];
    }
    if (LengthOut != 0) {
        *LengthOut = Length;
    }
    sNotifyHead = (sNotifyHead + 1u) % ATT_NOTIFY_QUEUE_DEPTH;
    sNotifyCount--;
    return 1;
}
