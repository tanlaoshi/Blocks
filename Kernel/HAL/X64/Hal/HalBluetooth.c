/*
 * HalBluetooth.c — x86 蓝牙门面（bt-facade / btx-1…3）
 *
 * NOTES（NUC 2026-10-27）：vid=8087 pid=0A2B class=E0 根口=08。
 * Worker Reveal 后 Probe → Reset → BD_ADDR + 名 Blocks；Ready 见 XhciBluetoothReady。
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
