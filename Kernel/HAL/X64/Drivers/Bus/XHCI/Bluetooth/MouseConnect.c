/*
 * MouseConnect.c — 一键 LE→SMP→HOGP→Notify→Input 后端（PR-BTM-8）
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"

#define MOUSE_CONNECT_DEFAULT_MS 20000u
#define LE_SCAN_LOOKUP_MAX 16u

int XhciBluetoothLeScanFindAddress(const UINT8 Address[6],
                                   UINTN *OneBasedIndexOut) {
    UINTN Index;
    UINT32 Byte;

    if (Address == 0 || OneBasedIndexOut == 0) {
        return -1;
    }
    for (Index = 1u; Index <= LE_SCAN_LOOKUP_MAX; Index++) {
        UINT8 Got[6];
        int Match = 1;

        if (BluetoothLeScanEntryGet(Index, Got, 0) < 0) {
            break;
        }
        for (Byte = 0; Byte < 6u; Byte++) {
            if (Got[Byte] != Address[Byte]) {
                Match = 0;
                break;
            }
        }
        if (Match) {
            *OneBasedIndexOut = Index;
            return 0;
        }
    }
    return -1;
}

int XhciBluetoothMouseConnect(UINTN OneBasedIndex, UINT32 TimeoutMs) {
    if (TimeoutMs == 0u) {
        TimeoutMs = MOUSE_CONNECT_DEFAULT_MS;
    }
    BluetoothLeLastErrorSet(0, 0);
    if (XhciBluetoothLeConnect(OneBasedIndex, TimeoutMs) < 0) {
        BootLog("bt: mouse fail at connect\n");
        return -1;
    }
    if (XhciBluetoothSmpJustWorks(TimeoutMs) < 0) {
        BootLog("bt: mouse fail at smp\n");
        return -1;
    }
    if (XhciBluetoothAttHidDiscover(TimeoutMs) < 0) {
        BootLog("bt: mouse fail at hogp\n");
        return -1;
    }
    if (XhciBluetoothAttHidSubscribe(TimeoutMs) < 0) {
        BootLog("bt: mouse fail at notify\n");
        return -1;
    }
    if (BluetoothHidInputAttach() < 0) {
        BootLog("bt: mouse fail at input\n");
        return -1;
    }
    BootLog("bt: mouse ok\n");
    return 0;
}
