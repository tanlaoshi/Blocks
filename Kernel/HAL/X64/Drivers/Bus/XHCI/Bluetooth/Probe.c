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
static int gBluetoothBringUpBusy;

int XhciBluetoothSeen(void) {
    return gBluetoothUsbSeen ? 1 : 0;
}

int XhciBluetoothHciResetOk(void) {
    return gBluetoothHciResetOk ? 1 : 0;
}

int XhciBluetoothReady(void) {
    return gBluetoothReady ? 1 : 0;
}

int XhciBluetoothProbeDone(void) {
    return (gBluetoothProbeDone && !gBluetoothBringUpBusy) ? 1 : 0;
}

/*
 * 壳 bt init：未 Ready 时清一次标记再探。
 * BringUpBusy 时拒重入（Worker 首探中）。
 */
int XhciBluetoothProbeRetry(void) {
    if (gBluetoothReady) {
        return 1;
    }
    if (gBluetoothBringUpBusy) {
        return 0;
    }
    BluetoothHostPumpStop();
    BluetoothReleaseSlot();
    gBluetoothConfigured = 0;
    gBluetoothHciResetOk = 0;
    gBluetoothReady = 0;
    gBluetoothUsbSeen = 0;
    gBluetoothVendorId = 0;
    gBluetoothProductId = 0;
    gBluetoothRootPort = 0;
    gBluetoothProbeDone = 0;
    return XhciBluetoothProbe();
}

int XhciBluetoothWaitReady(UINT32 TimeoutMilliseconds) {
    UINT64 Start;
    UINT64 Need;

    if (gBluetoothReady) {
        return 1;
    }
    if (TimeoutMilliseconds == 0u) {
        TimeoutMilliseconds = 5000u;
    }
    Start = ReadTsc();
    Need = (UINT64)TimeoutMilliseconds * 3000000ULL;
    while (!gBluetoothReady) {
        if (gBluetoothProbeDone && !gBluetoothBringUpBusy) {
            break;
        }
        StallMilliseconds(50);
        if (ReadTsc() - Start >= Need) {
            break;
        }
    }
    return gBluetoothReady ? 1 : 0;
}

void XhciBluetoothHostPump(void) {
    BluetoothHostPump();
}

int XhciBluetoothAclCreditQuery(UINT16 *PacketLengthOut,
                                UINT16 *PacketsFreeOut) {
    return BluetoothAclCreditQuery(PacketLengthOut, PacketsFreeOut);
}

int XhciBluetoothAclOutTest(void) {
    return BluetoothAclOutTest();
}

int XhciBluetoothCopyAddress(UINT8 Out[6]) {
    UINT32 I;

    if (Out == 0 || !gBluetoothReady) {
        return -1;
    }
    for (I = 0; I < 6u; I++) {
        Out[I] = gBluetoothAddress[I];
    }
    return 0;
}

int XhciBluetoothCopyLocalName(char *Out, UINTN NameMax) {
    UINTN I;

    if (Out == 0 || NameMax < 1u || !gBluetoothReady) {
        return -1;
    }
    for (I = 0; I + 1u < NameMax && gBluetoothLocalName[I]; I++) {
        Out[I] = gBluetoothLocalName[I];
    }
    Out[I] = 0;
    return 0;
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
    /* 与鼠/Wifi 共用 XhciRootPortIsOwned；BT 自身口未写入该表前可探 */
    return XhciRootPortIsOwned(Port);
}

static void BluetoothSurveyRoots(void) {
    UINT32 Port;
    UINT32 N = 0;

    BootLog("Boot: Bluetooth survey begin\n");
    for (Port = 1; Port <= gMaxPorts && Port <= 32u; Port++) {
        UINT32 Ps = ReadMmio32(gOperationalBase + PortRegister(Port));

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
    int Found = 0;

    if (gBluetoothReady) {
        return 1;
    }
    if (gBluetoothBringUpBusy) {
        return gBluetoothUsbSeen ? 1 : 0;
    }
    if (gBluetoothProbeDone) {
        return gBluetoothUsbSeen ? 1 : 0;
    }
    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        return -1;
    }

    gBluetoothBringUpBusy = 1;
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
                gBluetoothBringUpBusy = 0;
                return 0;
            }
            continue;
        }
        (void)Speed;
        if (BluetoothFinishProbe(Port, Speed)) {
            Found = 1;
            break;
        }
    }
    if (!Found) {
        BootLog("Boot: Bluetooth HCI not found\n");
    }
    BluetoothRequeueHid();
    gBluetoothBringUpBusy = 0;
    return Found;
}
