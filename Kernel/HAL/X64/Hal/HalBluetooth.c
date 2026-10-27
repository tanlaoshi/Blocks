/*
 * HalBluetooth.c — x86 蓝牙门面（bt-facade / btx-1）
 *
 * 课轨：NUC 板载蓝牙（内部 USB HCI，挂现网 xHCI）。
 * NOTES（本机手测 2026-10-27）：vid=8087 pid=0A2B class=E0 根口=08；
 *   冷启动常 CCS 无 PED（portsc=0x6E1）→ 须一次 Port Reset 再 Address。
 * btx-1/2：Worker Reveal 后 → Probe → Configure + HCI_Reset（不挡进桌面）；
 * Ready 仍 0 至 btx-3。
 */
#include "Hal.h"
#include "HalBluetooth.h"
#include "XHCI.h"

int HalBluetoothInitialize(void) {
    (void)XhciBluetoothProbe();
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
