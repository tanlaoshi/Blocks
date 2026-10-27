/*
 * HalBluetooth.c — Arm64 蓝牙门面（bt-facade）
 * Pi UART HCI 在 bt-pi；本刀无硬件 → Ready=0。
 */
#include "Hal.h"
#include "HalBluetooth.h"

int HalBluetoothInitialize(void) {
    return 0;
}

int HalBluetoothReady(void) {
    return 0;
}

int HalBluetoothGetDeviceAddress(UINT8 Out[HAL_BLUETOOTH_ADDRESS_LENGTH]) {
    UINT32 i;

    if (Out == 0) {
        return -1;
    }
    for (i = 0; i < HAL_BLUETOOTH_ADDRESS_LENGTH; i++) {
        Out[i] = 0;
    }
    return -1;
}

int HalBluetoothGetLocalName(char *Out, UINTN NameMax) {
    if (Out == 0 || NameMax < 1u) {
        return -1;
    }
    Out[0] = 0;
    return -1;
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
