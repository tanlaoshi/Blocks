/*
 * Probe.c — xHCI 根口认板载蓝牙 HCI（btx-1）
 *
 * Class 0xE0 或接口 E0/01/01。MSC claim 后调用。
 * 认到后 Configure + HCI_Reset（btx-2）；失败则 DisableSlot，Seen 仍保留。
 * 证据：NUC 口8 portsc=0x6E1（CCS 无 PED）→ 须一次 Port Reset，见 Address.c。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"

UINT8 gBluetoothDeviceContext[2048] BLOCKS_ALIGNED(64);
UINT8 gBluetoothConfigBuffer[BLUETOOTH_CONFIG_MAX] BLOCKS_ALIGNED(64);
UINT32 gBluetoothSlot;
int gBluetoothUsbSeen;
UINT16 gBluetoothVendorId;
UINT16 gBluetoothProductId;
UINT32 gBluetoothRootPort;
static int gBluetoothProbeDone;

int XhciBluetoothSeen(void) {
    return gBluetoothUsbSeen ? 1 : 0;
}

int XhciBluetoothHciResetOk(void) {
    return gBluetoothHciResetOk ? 1 : 0;
}

UINT16 XhciBluetoothVendorId(void) {
    return gBluetoothVendorId;
}

UINT16 XhciBluetoothProductId(void) {
    return gBluetoothProductId;
}

UINT32 XhciBluetoothRootPort(void) {
    return gBluetoothUsbSeen ? gBluetoothRootPort : 0;
}

static int BluetoothPortOwned(UINT32 Port) {
    if (gSlotId != 0 && Port == gPort1) {
        return 1;
    }
    if (gMouseSlotId != 0 && gMouseHubSlot == 0 && Port == gMousePort) {
        return 1;
    }
    if (gHubSlotId != 0 && Port == gHubRootPort) {
        return 1;
    }
    if (gMscPort != 0 && Port == gMscPort) {
        return 1;
    }
    if (gMscBootWatchPort != 0 && Port == gMscBootWatchPort) {
        return 1;
    }
    if (gFtdiClaimed && Port == gFtdiPort) {
        return 1;
    }
    if (gCdcClaimed && Port == gCdcPort) {
        return 1;
    }
    return 0;
}

static void BluetoothSurveyRoots(void) {
    UINT32 Port;
    UINT32 N = 0;

    BootLog("Boot: Bluetooth survey begin\n");
    for (Port = 1; Port <= gMaxPorts && Port <= 32u; Port++) {
        UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(Port));

        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        N++;
        BootLogHex("Boot: Bluetooth survey ccs port=", Port, 2);
        BootLogHex("Boot: Bluetooth survey portsc=", Ps, 8);
        BootLogHex("Boot: Bluetooth survey ped=",
                   (Ps & PORTSC_PED) ? 1u : 0u, 1);
        BootLogHex("Boot: Bluetooth survey spd=", PortSpeed(Ps), 1);
        if (BluetoothPortOwned(Port)) {
            BootLog("Boot: Bluetooth survey owned\n");
        } else {
            BootLog("Boot: Bluetooth survey free\n");
        }
    }
    BootLogHex("Boot: Bluetooth survey ccs count=", N, 2);
}

int XhciBluetoothProbe(void) {
    UINT32 Port;
    UINT8 Speed;

    if (gBluetoothProbeDone) {
        return gBluetoothUsbSeen ? 1 : 0;
    }
    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        return -1;
    }

    gBluetoothProbeDone = 1;
    BluetoothSurveyRoots();
    for (Port = 1; Port <= gMaxPorts && Port <= 32u; Port++) {
        if (BluetoothPortOwned(Port)) {
            continue;
        }
        if (!BluetoothAddressRoot(Port, &Speed)) {
            if (gXhciCmdSick) {
                BootLog("Boot: Bluetooth probe abort (cmd sick)\n");
                BluetoothRequeueHid();
                return 0;
            }
            continue;
        }
        (void)Speed;
        if (BluetoothFinishProbe(Port, Speed)) {
            BluetoothRequeueHid();
            return 1;
        }
    }
    BootLog("Boot: Bluetooth HCI not found\n");
    BluetoothRequeueHid();
    return 0;
}
