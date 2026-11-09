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
    if (XhciBluetoothReady()) {
        return 1;
    }
    if (!XhciBluetoothProbeDone()) {
        (void)XhciBluetoothProbe();
    } else {
        (void)XhciBluetoothProbeRetry();
    }
    return XhciBluetoothReady() ? 1 : 0;
}

int HalBluetoothReady(void) {
    return XhciBluetoothReady();
}

int HalBluetoothWaitReady(UINT32 TimeoutMilliseconds) {
    if (XhciBluetoothWaitReady(TimeoutMilliseconds)) {
        return 1;
    }
    /*
     * Worker 仍卡在 iwl 静默/Wifi：壳 bt 到时仍未首探。
     * 由壳拉一把；Busy 时 Probe 早退。
     */
    if (!XhciBluetoothProbeDone()) {
        (void)XhciBluetoothProbe();
    }
    return XhciBluetoothReady() ? 1 : 0;
}

void HalBluetoothBringUpStatus(int *ProbeDoneOut, int *UsbSeenOut,
                               int *HciResetOkOut, UINT16 *VendorIdOut,
                               UINT16 *ProductIdOut, UINT32 *RootPortOut) {
    if (ProbeDoneOut != 0) {
        *ProbeDoneOut = XhciBluetoothProbeDone();
    }
    if (UsbSeenOut != 0) {
        *UsbSeenOut = XhciBluetoothSeen();
    }
    if (HciResetOkOut != 0) {
        *HciResetOkOut = XhciBluetoothHciResetOk();
    }
    if (VendorIdOut != 0) {
        *VendorIdOut = XhciBluetoothVendorId();
    }
    if (ProductIdOut != 0) {
        *ProductIdOut = XhciBluetoothProductId();
    }
    if (RootPortOut != 0) {
        *RootPortOut = XhciBluetoothRootPort();
    }
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

void HalBluetoothHostPump(void) {
    XhciBluetoothHostPump();
}

int HalBluetoothAclCreditQuery(UINT16 *PacketLengthOut,
                               UINT16 *PacketsFreeOut) {
    return XhciBluetoothAclCreditQuery(PacketLengthOut, PacketsFreeOut);
}

int HalBluetoothAclOutTest(void) {
    return XhciBluetoothAclOutTest();
}

int HalBluetoothLeScan(HAL_BLUETOOTH_SCAN_ENTRY *Out, UINTN OutMax,
                       UINT32 TimeoutMs) {
    return XhciBluetoothLeScan(Out, OutMax, TimeoutMs);
}

int HalBluetoothLeConnect(UINTN OneBasedIndex, UINT32 TimeoutMs) {
    return XhciBluetoothLeConnect(OneBasedIndex, TimeoutMs);
}

int HalBluetoothLeDisconnect(void) {
    return XhciBluetoothLeDisconnect();
}

void HalBluetoothLeLastError(UINT16 *OpcodeOut, UINT8 *StatusOut) {
    XhciBluetoothLeLastError(OpcodeOut, StatusOut);
}

int HalBluetoothSmpJustWorks(UINT32 TimeoutMs) {
    return XhciBluetoothSmpJustWorks(TimeoutMs);
}
