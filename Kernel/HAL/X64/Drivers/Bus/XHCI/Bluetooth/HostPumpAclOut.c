/*
 * HostPumpAclOut.c — ACL Bulk OUT + Read_Buffer_Size 流控（PR-BTM-2）
 *
 * 额度：Read_Buffer_Size → Free；发送成功 Free--；NCP 归还。
 * 等待窗不占 Event Exclusive，泵与有线鼠可并行切片。
 */
#include "XHCI/Bluetooth/Private.h"

#define BLUETOOTH_ACL_OUT_BUFFER_BYTES 1024u

static UINT8 sAclOutDmaBuffer[BLUETOOTH_ACL_OUT_BUFFER_BYTES] BLOCKS_ALIGNED(64);
static UINT16 sAclPacketLengthMax;
static UINT16 sAclPacketsTotal;
static UINT16 sAclPacketsFree;
static int sAclFlowReady;

void BluetoothAclCreditReturn(UINT16 CompletedPackets) {
    UINT32 Sum;

    if (CompletedPackets == 0u || sAclPacketsTotal == 0u) {
        return;
    }
    Sum = (UINT32)sAclPacketsFree + (UINT32)CompletedPackets;
    if (Sum > (UINT32)sAclPacketsTotal) {
        Sum = sAclPacketsTotal;
    }
    sAclPacketsFree = (UINT16)Sum;
}

void BluetoothAclFlowReset(void) {
    sAclFlowReady = 0;
    sAclPacketLengthMax = 0;
    sAclPacketsTotal = 0;
    sAclPacketsFree = 0;
}

int BluetoothAclFlowInitialize(void) {
    UINT8 Status = 0xFFu;
    UINT8 ReturnData[8];
    UINT8 ReturnLength = 0;

    if (!gBluetoothReady) {
        return -1;
    }
    if (sAclFlowReady) {
        return 0;
    }
    ZeroMemory(ReturnData, sizeof(ReturnData));
    if (BluetoothHciCommand(HCI_OPCODE_READ_BUFFER_SIZE, 0, 0, &Status,
                            ReturnData, sizeof(ReturnData), &ReturnLength) < 0) {
        BootLog("bt: acl Read_Buffer_Size fail\n");
        return -1;
    }
    if (Status != 0 || ReturnLength < 7u) {
        BootLogHex("bt: acl Read_Buffer_Size status=", Status, 2);
        return -1;
    }
    sAclPacketLengthMax =
        (UINT16)(ReturnData[0] | ((UINT16)ReturnData[1] << 8));
    sAclPacketsTotal =
        (UINT16)(ReturnData[3] | ((UINT16)ReturnData[4] << 8));
    if (sAclPacketLengthMax == 0u) {
        sAclPacketLengthMax = 27u;
    }
    if (sAclPacketsTotal == 0u) {
        sAclPacketsTotal = 1u;
    }
    if (sAclPacketLengthMax > BLUETOOTH_ACL_OUT_BUFFER_BYTES - 4u) {
        sAclPacketLengthMax = (UINT16)(BLUETOOTH_ACL_OUT_BUFFER_BYTES - 4u);
    }
    sAclPacketsFree = sAclPacketsTotal;
    sAclFlowReady = 1;
    BootLogHex("bt: acl mtu=", sAclPacketLengthMax, 4);
    BootLogHex(" credit=", sAclPacketsTotal, 4);
    return 0;
}

int BluetoothAclCreditQuery(UINT16 *PacketLengthOut, UINT16 *PacketsFreeOut) {
    if (!sAclFlowReady) {
        if (BluetoothAclFlowInitialize() < 0) {
            return -1;
        }
    }
    if (PacketLengthOut) {
        *PacketLengthOut = sAclPacketLengthMax;
    }
    if (PacketsFreeOut) {
        *PacketsFreeOut = sAclPacketsFree;
    }
    return 0;
}

static void BluetoothAclOutEnqueue(UINT64 Physical, UINT32 Total) {
    UINT32 MaxPacketSize =
        gBluetoothAclOutMaxPacketSize ? gBluetoothAclOutMaxPacketSize : 64u;
    UINT32 Offset = 0;

    if (MaxPacketSize == 0u) {
        MaxPacketSize = 64u;
    }
    while (Offset < Total) {
        UINT32 Take = Total - Offset;
        UINT32 Control;

        if (Take > MaxPacketSize) {
            Take = MaxPacketSize;
        }
        if (Offset + Take < Total) {
            Control = TRB_TYPE(TRB_NORMAL) | TRB_CHAIN;
        } else {
            Control = TRB_TYPE(TRB_NORMAL) | TRB_IOC;
        }
        Enqueue(gBluetoothAclOutRing, &gBluetoothAclOut, Physical + Offset, Take,
                Control);
        Offset += Take;
    }
}

/*
 * 发一帧 HCI ACL Data（含 4 字节头）。ConnectionHandle 低 12 位有效。
 * 等待 USB OUT 完成；不持 Exclusive，泵可继续吃事件 / 鼠优先。
 */
int BluetoothAclOutSend(UINT16 ConnectionHandle, const UINT8 *Payload,
                        UINT16 PayloadLength) {
    UINT16 Header;
    UINT32 Total;
    UINT32 Index;
    UINT64 Start;
    UINT64 Need;
    int Result = -1;

    if (!gBluetoothConfigured || gBluetoothSlot == 0 ||
        gBluetoothAclOutDci == 0 || !gBluetoothReady) {
        return -1;
    }
    if (!sAclFlowReady && BluetoothAclFlowInitialize() < 0) {
        return -1;
    }
    if (PayloadLength > sAclPacketLengthMax) {
        return -1;
    }
    Total = 4u + (UINT32)PayloadLength;
    if (Total > BLUETOOTH_ACL_OUT_BUFFER_BYTES) {
        return -1;
    }
    Start = ReadTsc();
    Need = 2000ULL * 3000000ULL;
    while (sAclPacketsFree == 0u) {
        if (HalCpuIsHypervisor()) {
            ProcessEvents();
        } else {
            ProcessEventsRealPc();
        }
        BluetoothHostPump();
        ServiceHidCompletions();
        if (ReadTsc() - Start >= Need) {
            BootLog("bt: acl credit wait timeout\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    /* PB=10 start non-auto-flushable；BC=00 point-to-point */
    Header = (UINT16)((ConnectionHandle & 0x0FFFu) | (0x2u << 12));
    sAclOutDmaBuffer[0] = (UINT8)(Header & 0xFFu);
    sAclOutDmaBuffer[1] = (UINT8)((Header >> 8) & 0xFFu);
    sAclOutDmaBuffer[2] = (UINT8)(PayloadLength & 0xFFu);
    sAclOutDmaBuffer[3] = (UINT8)((PayloadLength >> 8) & 0xFFu);
    for (Index = 0; Index < PayloadLength; Index++) {
        sAclOutDmaBuffer[4u + Index] = Payload ? Payload[Index] : 0;
    }
    FlushDma(sAclOutDmaBuffer, Total);
    gBluetoothAclOutDone = 0;
    gBluetoothAclOutCode = 0;
    BluetoothAclOutEnqueue(PointerToPhysical(sAclOutDmaBuffer), Total);
    FlushDma(gBluetoothAclOutRing, sizeof(gBluetoothAclOutRing));
    RingDoorbell(gBluetoothSlot, gBluetoothAclOutDci);

    Start = ReadTsc();
    Need = 2000ULL * 3000000ULL;
    for (;;) {
        if (HalCpuIsHypervisor()) {
            ProcessEvents();
        } else {
            ProcessEventsRealPc();
        }
        BluetoothHostPump();
        ServiceHidCompletions();
        if (gBluetoothAclOutDone) {
            Result = (gBluetoothAclOutCode == CC_SUCCESS ||
                      gBluetoothAclOutCode == CC_SHORT_PACKET)
                         ? 0
                         : -1;
            break;
        }
        if (ReadTsc() - Start >= Need) {
            break;
        }
        __asm__ volatile("pause");
    }
    if (Result == 0 && sAclPacketsFree > 0u) {
        sAclPacketsFree--;
    }
    return Result;
}

int BluetoothAclOutTest(void) {
    UINT16 PacketLength = 0;
    UINT16 PacketsFree = 0;
    UINT8 Payload[4];

    if (!BluetoothHostPumpIsActive()) {
        BootLog("bt: acl need host pump\n");
        return -1;
    }
    if (BluetoothAclCreditQuery(&PacketLength, &PacketsFree) < 0) {
        return -1;
    }
    /* 无连接句柄：仅验证 Bulk OUT + 流控记账；控制器可丢弃 */
    Payload[0] = 0x00;
    Payload[1] = 0x00;
    Payload[2] = 0x00;
    Payload[3] = 0x00;
    if (BluetoothAclOutSend(0x0000u, Payload, 4) < 0) {
        BootLog("bt: acl out fail\n");
        return -1;
    }
    BootLog("bt: acl out ok\n");
    return 0;
}
