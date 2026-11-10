/*
 * MouseConnect.c — 一键 LE→SMP→HOGP→Notify→Input；断线重连（PR-BTM-8/10）
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"

#define MOUSE_CONNECT_DEFAULT_MS 20000u
#define LE_SCAN_LOOKUP_MAX 16u

static UINT8 sMouseLastAddress[6];
static UINT8 sMouseLastAddressType;
static int sMouseLastValid;

static void MouseLastRemember(const UINT8 Address[6], UINT8 AddressType) {
    UINT32 Byte;

    if (Address == 0) {
        return;
    }
    for (Byte = 0; Byte < 6u; Byte++) {
        sMouseLastAddress[Byte] = Address[Byte];
    }
    sMouseLastAddressType = AddressType;
    sMouseLastValid = 1;
}

void BluetoothMouseSessionTeardown(void) {
    BluetoothAttNotifyAcceptSet(0);
    BluetoothHidInputNoteArmed(0, 0);
    BluetoothAttNotifyQueueFlush();
    BluetoothAttHidDiscoveryClear();
    BluetoothAttMaximumTransmissionUnitReset();
    /* 断线半包会污染组装态 → 重连 Notify 错位/假 ok */
    BluetoothHostPumpAclInReset();
    BluetoothAclInboundFlush();
    BootLog("bt: mouse session down\n");
}

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

static int MouseConnectPipeline(UINT32 TimeoutMs) {
    UINT8 Address[6];
    UINT8 AddressType = 0;

    if (TimeoutMs == 0u) {
        TimeoutMs = MOUSE_CONNECT_DEFAULT_MS;
    }
    if (XhciBluetoothSmpJustWorks(TimeoutMs) < 0) {
        BootLog("bt: mouse fail at smp\n");
        BluetoothMouseSessionTeardown();
        return -1;
    }
    if (XhciBluetoothAttHidDiscover(TimeoutMs) < 0) {
        BootLog("bt: mouse fail at hogp\n");
        BluetoothMouseSessionTeardown();
        return -1;
    }
    if (XhciBluetoothAttHidSubscribe(TimeoutMs) < 0) {
        BootLog("bt: mouse fail at notify\n");
        BluetoothMouseSessionTeardown();
        return -1;
    }
    if (BluetoothHidInputAttach() < 0) {
        BootLog("bt: mouse fail at input\n");
        BluetoothMouseSessionTeardown();
        return -1;
    }
    BluetoothHostPumpAclInReset();
    if (gBluetoothAclInDci != 0) {
        BluetoothHostArmAclIn();
    }
    BluetoothAttNotifyAcceptSet(1);
    BluetoothRequeueHid();
    if (BluetoothLePeerAddressGet(Address, &AddressType) == 0) {
        MouseLastRemember(Address, AddressType);
    }
    BootLog("bt: mouse ok\n");
    return 0;
}

int XhciBluetoothMouseConnectAddress(const UINT8 Address[6], UINT8 AddressType,
                                     UINT32 TimeoutMs) {
    BluetoothLeLastErrorSet(0, 0);
    if (Address == 0) {
        return -1;
    }
    if (TimeoutMs == 0u) {
        TimeoutMs = MOUSE_CONNECT_DEFAULT_MS;
    }
    /* 重连前先拆旧会话（LeDisconnect  alone 不拆 HOGP） */
    BluetoothMouseSessionTeardown();
    MouseLastRemember(Address, AddressType);
    if (XhciBluetoothLeConnectAddress(Address, AddressType, TimeoutMs) < 0) {
        BootLog("bt: mouse fail at connect\n");
        return -1;
    }
    return MouseConnectPipeline(TimeoutMs);
}

int XhciBluetoothMouseConnect(UINTN OneBasedIndex, UINT32 TimeoutMs) {
    UINT8 Address[6];
    UINT8 AddressType = 0;

    BluetoothLeLastErrorSet(0, 0);
    if (TimeoutMs == 0u) {
        TimeoutMs = MOUSE_CONNECT_DEFAULT_MS;
    }
    if (BluetoothLeScanEntryGet(OneBasedIndex, Address, &AddressType) < 0) {
        BootLog("bt: mouse fail bad index (lescan first)\n");
        return -1;
    }
    return XhciBluetoothMouseConnectAddress(Address, AddressType, TimeoutMs);
}

int XhciBluetoothMouseReconnect(UINT32 TimeoutMs) {
    UINT8 Address[6];
    UINT8 AddressType = 0;

    BluetoothLeLastErrorSet(0, 0);
    if (sMouseLastValid) {
        AddressType = sMouseLastAddressType;
        {
            UINT32 Byte;
            for (Byte = 0; Byte < 6u; Byte++) {
                Address[Byte] = sMouseLastAddress[Byte];
            }
        }
    } else if (BluetoothLeLastPeerAddressGet(Address, &AddressType) < 0) {
        BootLog("bt: mouse reconnect need prior mouse / peer\n");
        return -1;
    }
    BootLog("bt: mouse reconnect\n");
    return XhciBluetoothMouseConnectAddress(Address, AddressType, TimeoutMs);
}

int XhciBluetoothMouseDisconnect(void) {
    int Result;

    Result = XhciBluetoothLeDisconnect();
    BluetoothMouseSessionTeardown();
    return Result;
}
