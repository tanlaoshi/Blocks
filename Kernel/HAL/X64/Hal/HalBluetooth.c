/*
 * HalBluetooth.c — x86 蓝牙门面（bt-facade）
 *
 * 课轨硬件：NUC 板载蓝牙（实现上常见内部 USB HCI，挂现网 xHCI）。
 * 本刀只挂门面：无探针 → Ready=0。真枚举 / Reset / 扫 → btx-1…5。
 */
#include "Hal.h"
#include "HalBluetooth.h"

int HalBluetoothInitialize(void) {
    /* 门面就位；控制器留给 btx-1 */
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
