/*
 * Visible.c — 可被发现（btx-5）
 *
 * Write_Scan_Enable Inquiry+Page；EIR 带名 Blocks；CoD=Computer。
 * 课堂：手机蓝牙扫描应见「Blocks」。不做 A2DP/HID-over-BT。
 *
 * 口述：Ready 后（或 bt visible）→ 手机开蓝牙搜「Blocks」→ 见名即过。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"

int gBluetoothDiscoverable;

static int WriteClassComputer(void) {
    UINT8 ClassOfDevice[3];
    UINT8 Status = 0xFF;

    /* Major=Computer (0x01)，Minor=Uncategorized */
    ClassOfDevice[0] = 0x0C;
    ClassOfDevice[1] = 0x01;
    ClassOfDevice[2] = 0x00;
    if (BluetoothHciCommand(HCI_OPCODE_WRITE_CLASS_OF_DEVICE, ClassOfDevice, 3,
                            &Status, 0, 0, 0) < 0) {
        return 0;
    }
    return Status == 0;
}

static int WriteEirNameBlocks(void) {
    UINT8 Params[241];
    UINT8 Status = 0xFF;
    const char *Name = "Blocks";
    UINT32 I;
    UINT8 NameLen = 0;

    while (Name[NameLen] && NameLen < 60u) {
        NameLen++;
    }
    ZeroMemory(Params, sizeof(Params));
    Params[0] = 0; /* FEC not required */
    /* EIR：Complete Local Name */
    Params[1] = (UINT8)(1u + NameLen);
    Params[2] = HCI_EIR_NAME_COMPLETE;
    for (I = 0; I < NameLen; I++) {
        Params[3u + I] = (UINT8)Name[I];
    }
    if (BluetoothHciCommand(HCI_OPCODE_WRITE_EXTENDED_INQUIRY_RESPONSE, Params,
                            241, &Status, 0, 0, 0) < 0) {
        return 0;
    }
    return Status == 0;
}

static int WriteScanEnable(UINT8 Enable) {
    UINT8 Status = 0xFF;

    if (BluetoothHciCommand(HCI_OPCODE_WRITE_SCAN_ENABLE, &Enable, 1, &Status, 0,
                            0, 0) < 0) {
        return 0;
    }
    return Status == 0;
}

int BluetoothSetDiscoverable(int On) {
    if (!gBluetoothReady) {
        return 0;
    }
    if (On) {
        (void)WriteClassComputer();
        (void)WriteEirNameBlocks();
        if (!WriteScanEnable(0x03)) {
            BootLog("Boot: Bluetooth Scan_Enable fail\n");
            gBluetoothDiscoverable = 0;
            return 0;
        }
        gBluetoothDiscoverable = 1;
        BootLog("Boot: Bluetooth discoverable name=Blocks\n");
        return 1;
    }
    if (!WriteScanEnable(0x00)) {
        BootLog("Boot: Bluetooth Scan_Enable off fail\n");
        return 0;
    }
    gBluetoothDiscoverable = 0;
    BootLog("Boot: Bluetooth hidden\n");
    return 1;
}

int XhciBluetoothDiscoverable(void) {
    return gBluetoothDiscoverable ? 1 : 0;
}

int XhciBluetoothSetDiscoverable(int On) {
    return BluetoothSetDiscoverable(On) ? 0 : -1;
}
