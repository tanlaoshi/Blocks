/*
 * HostPumpAclIn.c — ACL Bulk IN 常驻臂 + 组装（PR-BTM-1）
 *
 * 专用 DMA 缓冲，勿与事件 Interrupt 共用 gBluetoothEventBuffer。
 * 组装满 HCI ACL 头后丢弃体（业务投递 BTM-2+）；证明 IN 路径活着。
 */
#include "XHCI/Bluetooth/Private.h"

#define BLUETOOTH_ACL_IN_BUFFER_BYTES 1024u

static UINT8 sAclInUsbBuffer[BLUETOOTH_ACL_IN_BUFFER_BYTES] BLOCKS_ALIGNED(64);
static UINT8 sAclAssemble[BLUETOOTH_ACL_IN_BUFFER_BYTES];
static UINT32 sAclAssembleBytes;

void BluetoothHostPumpAclInReset(void) {
    sAclAssembleBytes = 0;
}

void BluetoothHostArmAclIn(void) {
    UINT32 Len = gBluetoothAclInMaxPacketSize ? gBluetoothAclInMaxPacketSize
                                              : 64u;

    if (!gBluetoothConfigured || gBluetoothAclInDci == 0) {
        return;
    }
    if (Len > BLUETOOTH_ACL_IN_BUFFER_BYTES) {
        Len = BLUETOOTH_ACL_IN_BUFFER_BYTES;
    }
    ZeroMemory(sAclInUsbBuffer, Len);
    FlushDma(sAclInUsbBuffer, Len);
    gBluetoothAclInDone = 0;
    gBluetoothAclInCode = 0;
    gBluetoothAclInRemain = 0;
    Enqueue(gBluetoothAclInRing, &gBluetoothAclIn,
            PointerToPhysical(sAclInUsbBuffer), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    FlushDma(gBluetoothAclInRing, sizeof(gBluetoothAclInRing));
    RingDoorbell(gBluetoothSlot, gBluetoothAclInDci);
}

void BluetoothHostPumpConsumeAclIn(void) {
    static int sAclInConsuming;
    UINT32 Requested;
    UINT32 Got;
    UINT32 Index;
    UINT32 Need;

    /*
     * AclOutSend 等待窗会再调 HostPump；若此处无门闩，会二次吃同一 Done、
     * 双 Arm ACL IN → 环坏，有线+BT 鼠一起死。
     */
    if (sAclInConsuming || !gBluetoothAclInDone) {
        return;
    }
    sAclInConsuming = 1;
    if (!(gBluetoothAclInCode == CC_SUCCESS ||
          gBluetoothAclInCode == CC_SHORT_PACKET)) {
        gBluetoothAclInDone = 0;
        BluetoothHostArmAclIn();
        sAclInConsuming = 0;
        return;
    }
    Requested = gBluetoothAclInMaxPacketSize ? gBluetoothAclInMaxPacketSize
                                             : 64u;
    if (Requested > BLUETOOTH_ACL_IN_BUFFER_BYTES) {
        Requested = BLUETOOTH_ACL_IN_BUFFER_BYTES;
    }
    Got = Requested;
    if (gBluetoothAclInRemain < Requested) {
        Got = Requested - gBluetoothAclInRemain;
    }
    FlushDma(sAclInUsbBuffer, Requested);
    if (sAclAssembleBytes + Got > BLUETOOTH_ACL_IN_BUFFER_BYTES) {
        sAclAssembleBytes = 0;
    }
    for (Index = 0; Index < Got; Index++) {
        sAclAssemble[sAclAssembleBytes + Index] = sAclInUsbBuffer[Index];
    }
    sAclAssembleBytes += Got;
    while (sAclAssembleBytes >= 4u) {
        Need = 4u + (UINT32)(sAclAssemble[2] | (sAclAssemble[3] << 8));
        if (Need > BLUETOOTH_ACL_IN_BUFFER_BYTES) {
            sAclAssembleBytes = 0;
            break;
        }
        if (sAclAssembleBytes < Need) {
            break;
        }
        /* 完整 HCI ACL：头 4 + 载荷；SMP 取 L2CAP 体 */
        if (Need > 4u) {
            BluetoothAclInboundPush(&sAclAssemble[4], (UINT16)(Need - 4u));
            /* 每帧 HCI 后切片 HID，避免一缓冲多 Notify 饿死有线鼠 */
            ServiceHidCompletions();
        }
        for (Index = Need; Index < sAclAssembleBytes; Index++) {
            sAclAssemble[Index - Need] = sAclAssemble[Index];
        }
        sAclAssembleBytes -= Need;
    }
    gBluetoothAclInDone = 0;
    /* 先按完成再武装 HID，再臂下一笔 BT ACL IN（勿 Queue* 双投递） */
    ServiceHidCompletions();
    BluetoothHostArmAclIn();
    sAclInConsuming = 0;
}
