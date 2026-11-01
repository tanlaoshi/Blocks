/*
 * HalBluetooth.c — Arm64 蓝牙门面（bt-pi-2）
 * Pi：UART HCI + .hcd → Reset + BD_ADDR 成功才 Ready=1。
 */
#include "Hal.h"
#include "BoardConfig.h" /* BOARD_HAS_BLUETOOTH（bt-pi） */
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
