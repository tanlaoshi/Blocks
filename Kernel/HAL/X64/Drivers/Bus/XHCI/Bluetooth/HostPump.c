/*
 * HostPump.c — HCI 常驻泵（PR-BTM-1）
 *
 * 事件 Interrupt IN → 软件队列；Number of Completed Packets 记账；
 * ACL IN 见 HostPumpAclIn.c。每切片 ServiceHidCompletions（鼠优先）。
 * Ready 前仍走单缓冲 Arm/Wait（固件加载）；仅 Ready 后 Start。
 * Active 用魔数，禁止「非 0 即真」（防 BSS 垃圾抢 Arm）。
 */
#include "XHCI/Bluetooth/Private.h"

#define BLUETOOTH_HOST_EVENT_QUEUE_DEPTH 16u

UINT32 gBluetoothHostPumpActive;
UINT32 gBluetoothAclPacketsCompleted;
volatile UINT32 gBluetoothEventRemain;
volatile UINT32 gBluetoothAclInRemain;

static UINT8 sEventQueue[BLUETOOTH_HOST_EVENT_QUEUE_DEPTH][BLUETOOTH_EVENT_MAX];
static UINT8 sEventLength[BLUETOOTH_HOST_EVENT_QUEUE_DEPTH];
static UINT32 sEventHead;
static UINT32 sEventTail;
static UINT32 sEventCount;

static void EventQueueReset(void) {
    sEventHead = 0;
    sEventTail = 0;
    sEventCount = 0;
}

static int EventQueuePush(const UINT8 *Data, UINT32 Length) {
    UINT32 Index;

    if (Data == 0 || Length == 0u || Length > BLUETOOTH_EVENT_MAX) {
        return 0;
    }
    if (sEventCount >= BLUETOOTH_HOST_EVENT_QUEUE_DEPTH) {
        return 0;
    }
    for (Index = 0; Index < Length; Index++) {
        sEventQueue[sEventTail][Index] = Data[Index];
    }
    sEventLength[sEventTail] = (UINT8)Length;
    sEventTail = (sEventTail + 1u) % BLUETOOTH_HOST_EVENT_QUEUE_DEPTH;
    sEventCount++;
    return 1;
}

int BluetoothHostEventTake(UINT8 *Out, UINT32 Capacity, UINT32 *OutLength) {
    UINT32 Length;
    UINT32 Index;

    if (Out == 0 || Capacity == 0u || sEventCount == 0u) {
        return 0;
    }
    Length = sEventLength[sEventHead];
    if (Length > Capacity) {
        Length = Capacity;
    }
    for (Index = 0; Index < Length; Index++) {
        Out[Index] = sEventQueue[sEventHead][Index];
    }
    if (OutLength) {
        *OutLength = Length;
    }
    sEventHead = (sEventHead + 1u) % BLUETOOTH_HOST_EVENT_QUEUE_DEPTH;
    sEventCount--;
    return 1;
}

void BluetoothHostEventQueueFlush(void) {
    EventQueueReset();
}

static void ApplyNumberOfCompletedPackets(const UINT8 *Event, UINT32 Length) {
    UINT8 HandleCount;
    UINT8 Offset;
    UINT8 Index;

    if (Event == 0 || Length < 3u || Event[1] < 1u) {
        return;
    }
    HandleCount = Event[2];
    Offset = 3;
    for (Index = 0; Index < HandleCount; Index++) {
        UINT16 Completed;

        if ((UINT32)Offset + 4u > Length) {
            break;
        }
        Completed = (UINT16)(Event[Offset + 2u] | (Event[Offset + 3u] << 8));
        gBluetoothAclPacketsCompleted += Completed;
        BluetoothAclCreditReturn(Completed);
        Offset = (UINT8)(Offset + 4u);
    }
}

void BluetoothHostArmEventEndpoint(void) {
    UINT32 Len = gBluetoothEventMaxPacketSize ? gBluetoothEventMaxPacketSize
                                              : 16u;

    if (!gBluetoothConfigured || gBluetoothEventDci == 0) {
        return;
    }
    if (Len > BLUETOOTH_EVENT_MAX) {
        Len = BLUETOOTH_EVENT_MAX;
    }
    ZeroMemory(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    FlushDma(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    gBluetoothEventDone = 0;
    gBluetoothEventCode = 0;
    gBluetoothEventRemain = 0;
    Enqueue(gBluetoothEventRing, &gBluetoothEvent,
            PointerToPhysical(gBluetoothEventBuffer), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    FlushDma(gBluetoothEventRing, sizeof(gBluetoothEventRing));
    RingDoorbell(gBluetoothSlot, gBluetoothEventDci);
}

void BluetoothHostPumpConsumeEvent(void) {
    UINT32 Requested;
    UINT32 Got;
    UINT32 Length;

    if (!gBluetoothEventDone) {
        return;
    }
    if (!(gBluetoothEventCode == CC_SUCCESS ||
          gBluetoothEventCode == CC_SHORT_PACKET)) {
        gBluetoothEventDone = 0;
        BluetoothHostArmEventEndpoint();
        return;
    }
    Requested = gBluetoothEventMaxPacketSize ? gBluetoothEventMaxPacketSize
                                             : 16u;
    if (Requested > BLUETOOTH_EVENT_MAX) {
        Requested = BLUETOOTH_EVENT_MAX;
    }
    Got = Requested;
    if (gBluetoothEventRemain < Requested) {
        Got = Requested - gBluetoothEventRemain;
    }
    FlushDma(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    Length = 2u + (UINT32)gBluetoothEventBuffer[1];
    if (Length > Got) {
        Length = Got;
    }
    if (Length >= 2u &&
        gBluetoothEventBuffer[0] == HCI_EVENT_NUMBER_OF_COMPLETED_PACKETS) {
        ApplyNumberOfCompletedPackets(gBluetoothEventBuffer, Length);
    } else {
        (void)EventQueuePush(gBluetoothEventBuffer, Length);
    }
    gBluetoothEventDone = 0;
    BluetoothHostArmEventEndpoint();
}

/*
 * 同步 Wait 窗内：Exclusive 已持 → Drain/Irq 不跑泵；本函数把 Done 入队，
 * 并把首个非 NCP 事件拷回 gBluetoothEventBuffer 供 Command/Scan 解析。
 */
int BluetoothHostPumpWaitTakeEvent(void) {
    UINT32 Length = 0;

    BluetoothHostPumpConsumeEvent();
    if (!BluetoothHostEventTake(gBluetoothEventBuffer, BLUETOOTH_EVENT_MAX,
                                &Length)) {
        return 0;
    }
    if (Length < BLUETOOTH_EVENT_MAX) {
        ZeroMemory(gBluetoothEventBuffer + Length,
                   BLUETOOTH_EVENT_MAX - Length);
    }
    gBluetoothEventDone = 1;
    gBluetoothEventCode = CC_SUCCESS;
    return 1;
}

void BluetoothHostPump(void) {
    if (!BluetoothHostPumpIsActive() || !gBluetoothConfigured) {
        return;
    }
    /* 同步 HCI Exclusive 窗：勿抢 Done / 勿盖写事件缓冲 */
    if (EventIsExclusive()) {
        return;
    }
    BluetoothHostPumpConsumeEvent();
    BluetoothHostPumpConsumeAclIn();
    ServiceHidCompletions();
}

int BluetoothHostPumpStart(void) {
    if (BluetoothHostPumpIsActive()) {
        return 0;
    }
    if (!gBluetoothConfigured || !gBluetoothReady || gBluetoothEventDci == 0) {
        return -1;
    }
    EventQueueReset();
    BluetoothHostPumpAclInReset();
    BluetoothAclFlowReset();
    gBluetoothAclPacketsCompleted = 0;
    gBluetoothHostPumpActive = BLUETOOTH_HOST_PUMP_ON;
    BluetoothHostArmEventEndpoint();
    if (gBluetoothAclInDci != 0) {
        BluetoothHostArmAclIn();
    }
    BootLog("bt: host pump\n");
    /* PR-BTM-2：泵起来后再读额度，命令走 WaitTake 与泵并行 */
    (void)BluetoothAclFlowInitialize();
    return 0;
}

void BluetoothHostPumpStop(void) {
    gBluetoothHostPumpActive = 0;
    EventQueueReset();
    BluetoothHostPumpAclInReset();
    BluetoothAclFlowReset();
}
