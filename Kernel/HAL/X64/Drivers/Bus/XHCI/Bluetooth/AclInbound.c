/* AclInbound.c — ACL 载荷队列（SMP/ATT 取用）；鼠优先由泵切片保证 */
#include "XHCI/Bluetooth/Private.h"
#define BLUETOOTH_ACL_INBOUND_DEPTH 8u
#define BLUETOOTH_ACL_INBOUND_MAX 128u
static UINT8 sInbound[BLUETOOTH_ACL_INBOUND_DEPTH][BLUETOOTH_ACL_INBOUND_MAX];
static UINT16 sInboundLength[BLUETOOTH_ACL_INBOUND_DEPTH];
static UINT32 sInboundHead;
static UINT32 sInboundTail;
static UINT32 sInboundCount;
void BluetoothAclInboundFlush(void) {
    sInboundHead = 0;
    sInboundTail = 0;
    sInboundCount = 0;
}
void BluetoothAclInboundPush(const UINT8 *Payload, UINT16 Length) {
    UINT32 Index;
    if (Payload == 0 || Length == 0u || Length > BLUETOOTH_ACL_INBOUND_MAX) {
        return;
    }
    /* ATT Notify/Indicate 进专用环，勿堵 SMP/ATT 应答队列 */
    if (BluetoothAttNotifyQueueIsAttributeNotification(Payload, Length)) {
        BluetoothAttNotifyQueuePushAcl(Payload, Length);
        return;
    }
    /* L2CAP LE 信号：Conn Param Update 立即应答，勿进业务队列 */
    if (BluetoothLeLinkL2capSignalPush(Payload, Length)) {
        return;
    }
    if (sInboundCount >= BLUETOOTH_ACL_INBOUND_DEPTH) {
        return;
    }
    for (Index = 0; Index < Length; Index++) {
        sInbound[sInboundTail][Index] = Payload[Index];
    }
    sInboundLength[sInboundTail] = Length;
    sInboundTail = (sInboundTail + 1u) % BLUETOOTH_ACL_INBOUND_DEPTH;
    sInboundCount++;
}
int BluetoothAclInboundTake(UINT8 *Out, UINT16 Capacity, UINT16 *OutLength) {
    UINT16 Length;
    UINT32 Index;
    if (Out == 0 || Capacity == 0u || sInboundCount == 0u) {
        return 0;
    }
    Length = sInboundLength[sInboundHead];
    if (Length > Capacity) {
        Length = Capacity;
    }
    for (Index = 0; Index < Length; Index++) {
        Out[Index] = sInbound[sInboundHead][Index];
    }
    if (OutLength != 0) {
        *OutLength = Length;
    }
    sInboundHead = (sInboundHead + 1u) % BLUETOOTH_ACL_INBOUND_DEPTH;
    sInboundCount--;
    return 1;
}
