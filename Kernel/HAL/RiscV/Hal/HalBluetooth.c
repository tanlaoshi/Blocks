/*
 * HalBluetooth.c — RiscV 蓝牙门面（bt-duo-1）
 * Duo：UART4 HCI；Ready 仅当 Reset+BD_ADDR 成功。
 */
#include "Hal.h"
#include "BoardConfig.h"
#include "HalBluetooth.h"

#ifndef BOARD_HAS_BLUETOOTH
#define BOARD_HAS_BLUETOOTH 0
#endif

#if BOARD_HAS_BLUETOOTH
int BoardBluetoothBringUp(void);
int BoardBluetoothReady(void);
int BoardBluetoothCopyDeviceAddress(UINT8 Out[HAL_BLUETOOTH_ADDRESS_LENGTH]);
int BoardBluetoothCopyLocalName(char *Out, UINTN NameMax);
#endif

int HalBluetoothInitialize(void) {
#if BOARD_HAS_BLUETOOTH
    return BoardBluetoothBringUp() == 0 ? 1 : 0;
#else
    return 0;
#endif
}

int HalBluetoothReady(void) {
#if BOARD_HAS_BLUETOOTH
    return BoardBluetoothReady();
#else
    return 0;
#endif
}

int HalBluetoothWaitReady(UINT32 TimeoutMilliseconds) {
    (void)TimeoutMilliseconds;
    return HalBluetoothReady();
}

void HalBluetoothBringUpStatus(int *ProbeDoneOut, int *UsbSeenOut,
                               int *HciResetOkOut, UINT16 *VendorIdOut,
                               UINT16 *ProductIdOut, UINT32 *RootPortOut) {
    if (ProbeDoneOut != 0) {
        *ProbeDoneOut = 1;
    }
    if (UsbSeenOut != 0) {
        *UsbSeenOut = HalBluetoothReady();
    }
    if (HciResetOkOut != 0) {
        *HciResetOkOut = HalBluetoothReady();
    }
    if (VendorIdOut != 0) {
        *VendorIdOut = 0;
    }
    if (ProductIdOut != 0) {
        *ProductIdOut = 0;
    }
    if (RootPortOut != 0) {
        *RootPortOut = 0;
    }
}

int HalBluetoothGetDeviceAddress(UINT8 Out[HAL_BLUETOOTH_ADDRESS_LENGTH]) {
#if BOARD_HAS_BLUETOOTH
    return BoardBluetoothCopyDeviceAddress(Out);
#else
    UINT32 Index;

    if (Out == 0) {
        return -1;
    }
    for (Index = 0u; Index < HAL_BLUETOOTH_ADDRESS_LENGTH; Index++) {
        Out[Index] = 0;
    }
    return -1;
#endif
}

int HalBluetoothGetLocalName(char *Out, UINTN NameMax) {
#if BOARD_HAS_BLUETOOTH
    return BoardBluetoothCopyLocalName(Out, NameMax);
#else
    if (Out == 0 || NameMax < 1u) {
        return -1;
    }
    Out[0] = 0;
    return -1;
#endif
}

int HalBluetoothScan(HAL_BLUETOOTH_SCAN_ENTRY *Out, UINTN OutMax,
                     UINT32 TimeoutMs) {
    (void)Out;
    (void)OutMax;
    (void)TimeoutMs;
    return -1;
}

int HalBluetoothDiscoverable(void) {
    return 0;
}

int HalBluetoothSetDiscoverable(int On) {
    (void)On;
    return -1;
}

void HalBluetoothHostPump(void) {
}

int HalBluetoothAclCreditQuery(UINT16 *PacketLengthOut,
                               UINT16 *PacketsFreeOut) {
    if (PacketLengthOut) {
        *PacketLengthOut = 0;
    }
    if (PacketsFreeOut) {
        *PacketsFreeOut = 0;
    }
    return -1;
}

int HalBluetoothAclOutTest(void) {
    return -1;
}

int HalBluetoothLeScan(HAL_BLUETOOTH_SCAN_ENTRY *Out, UINTN OutMax,
                       UINT32 TimeoutMs) {
    (void)Out;
    (void)OutMax;
    (void)TimeoutMs;
    return -1;
}

int HalBluetoothLeConnect(UINTN OneBasedIndex, UINT32 TimeoutMs) {
    (void)OneBasedIndex;
    (void)TimeoutMs;
    return -1;
}

int HalBluetoothLeDisconnect(void) {
    return -1;
}

int HalBluetoothMouseDisconnect(void) {
    return -1;
}

void HalBluetoothLeLastError(UINT16 *OpcodeOut, UINT8 *StatusOut) {
    if (OpcodeOut != 0) {
        *OpcodeOut = 0;
    }
    if (StatusOut != 0) {
        *StatusOut = 0;
    }
}

int HalBluetoothSmpJustWorks(UINT32 TimeoutMs) {
    (void)TimeoutMs;
    return -1;
}

int HalBluetoothAttHidDiscover(UINT32 TimeoutMs) {
    (void)TimeoutMs;
    return -1;
}

int HalBluetoothAttHidSubscribe(UINT32 TimeoutMs) {
    (void)TimeoutMs;
    return -1;
}

int HalBluetoothMouseConnect(UINTN OneBasedIndex, UINT32 TimeoutMs) {
    (void)OneBasedIndex;
    (void)TimeoutMs;
    return -1;
}

int HalBluetoothMouseReconnect(UINT32 TimeoutMs) {
    (void)TimeoutMs;
    return -1;
}

int HalBluetoothLeScanFindAddress(
    const UINT8 Address[HAL_BLUETOOTH_ADDRESS_LENGTH],
    UINTN *OneBasedIndexOut) {
    (void)Address;
    if (OneBasedIndexOut != 0) {
        *OneBasedIndexOut = 0;
    }
    return -1;
}
