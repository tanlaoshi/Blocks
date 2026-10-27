/*
 * HalBluetooth.c — x86 蓝牙门面（bt-facade / btx-1…5）
 *
 * NOTES（NUC 2026-10-27）：vid=8087 pid=0A2B class=E0 根口=08。
 * 口述：桌面出齐后 HCI Ready → 默认可发现；手机蓝牙搜「Blocks」见名即过。
 * 壳：bt / bt scan / bt visible / bt hide。
 */
#include "Hal.h"
#include "HalBluetooth.h"
#include "XHCI.h"

int HalBluetoothInitialize(void) {
    (void)XhciBluetoothProbe();
    return 0;
}

int HalBluetoothReady(void) {
    return XhciBluetoothReady();
}

int HalBluetoothGetDeviceAddress(UINT8 Out[HAL_BLUETOOTH_ADDRESS_LENGTH]) {
    UINT32 I;

    if (Out == 0) {
        return -1;
    }
    if (XhciBluetoothCopyAddress(Out) == 0) {
        return 0;
    }
    for (I = 0; I < HAL_BLUETOOTH_ADDRESS_LENGTH; I++) {
        Out[I] = 0;
    }
    return -1;
}

int HalBluetoothGetLocalName(char *Out, UINTN NameMax) {
    if (Out == 0 || NameMax < 1u) {
        return -1;
    }
    if (XhciBluetoothCopyLocalName(Out, NameMax) == 0) {
        return 0;
    }
    Out[0] = 0;
    return -1;
}

int HalBluetoothScan(HAL_BLUETOOTH_SCAN_ENTRY *Out, UINTN OutMax,
                     UINT32 TimeoutMs) {
    return XhciBluetoothScan(Out, OutMax, TimeoutMs);
}

int HalBluetoothDiscoverable(void) {
    return XhciBluetoothDiscoverable();
}

int HalBluetoothSetDiscoverable(int On) {
    return XhciBluetoothSetDiscoverable(On);
}
