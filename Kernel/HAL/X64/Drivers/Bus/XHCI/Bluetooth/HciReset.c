/*
 * HciReset.c — HCI_Reset + Intel bootloader 灌固件入口
 *
 * 8087:0A2B：冷启多在 bootloader；对表 btusb INTEL_NEW 先灌 .sfi 再 Reset。
 */
#include "XHCI/Bluetooth/Private.h"

static void BluetoothProbeIntelVersion(void) {
    UINT8 Status = 0xFFu;
    UINT8 Ver[12];
    UINT8 RetLen = 0;
    UINT32 I;

    ZeroMemory(Ver, sizeof(Ver));
    if (BluetoothHciCommand(HCI_OPCODE_INTEL_VERSION, 0, 0, &Status, Ver,
                            sizeof(Ver), &RetLen) < 0) {
        BootLog("Boot: Bluetooth Intel ver send/event fail\n");
        return;
    }
    BootLogHex("Boot: Bluetooth Intel ver status=", Status, 2);
    BootLogHex(" retlen=", RetLen, 2);
    for (I = 0; I < RetLen && I < sizeof(Ver); I++) {
        BootLogHex(" v=", Ver[I], 2);
    }
    if (Status == 0 && RetLen >= 4u) {
        BootLogHex("Boot: Bluetooth Intel fw_variant=", Ver[3], 2);
        if (Ver[3] == 0x06u) {
            BootLog("Boot: Bluetooth Intel mode=bootloader (need fw)\n");
        } else if (Ver[3] == 0x23u) {
            BootLog("Boot: Bluetooth Intel mode=operational\n");
        } else {
            BootLog("Boot: Bluetooth Intel mode=other\n");
        }
    }
}

int BluetoothHciReset(void) {
    UINT8 Status = 0xFF;
    int Attempt;

    gBluetoothHciResetOk = 0;
    gBluetoothReady = 0;
    /*
     * 8087:0A2B bootloader：标准 Reset 常 status≠0；真机曾 Interrupt 无 CC。
     * 对表 btusb INTEL_NEW：直接灌固件/读版本，不先空耗一条 Reset。
     * 软 reboot 后若再哑火须冷启（见排障档）。
     */
    if (gBluetoothVendorId == 0x8087u && gBluetoothProductId == 0x0A2Bu) {
        if (BluetoothIntelFirmwareLoad()) {
            Status = 0xFF;
            if (BluetoothHciCommand(HCI_OPCODE_RESET, 0, 0, &Status, 0, 0, 0) ==
                    0 &&
                Status == 0) {
                gBluetoothHciResetOk = 1;
                BootLog("Boot: Bluetooth HCI Reset ok (after fw)\n");
                return 1;
            }
            BootLogHex("Boot: Bluetooth post-fw Reset status=", Status, 2);
        }
        BluetoothProbeIntelVersion();
        return 0;
    }
    for (Attempt = 0; Attempt < 2; Attempt++) {
        Status = 0xFF;
        if (BluetoothHciCommand(HCI_OPCODE_RESET, 0, 0, &Status, 0, 0, 0) < 0) {
            BootLog("Boot: Bluetooth HCI Reset send/event fail\n");
        } else {
            BootLogHex("Boot: Bluetooth CC opcode=", HCI_OPCODE_RESET, 4);
            BootLogHex(" status=", Status, 2);
            if (Status == 0) {
                gBluetoothHciResetOk = 1;
                BootLog("Boot: Bluetooth HCI Reset ok\n");
                return 1;
            }
            BootLog("Boot: Bluetooth HCI Reset bad status\n");
        }
        if (Attempt == 0) {
            if (!BluetoothIntelFirmwareLoad()) {
                BluetoothProbeIntelVersion();
                return 0;
            }
        }
    }
    BluetoothProbeIntelVersion();
    return 0;
}
