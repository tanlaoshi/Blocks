/*
 * LeConnect.c — LE Disconnect / Abort；按序号连转 Address（PR-BTM-4/10）
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"

#define LE_CONNECT_SLICE_MS 50u
#define LE_DISCONNECT_DEFAULT_MS 5000u
#define HCI_REASON_REMOTE_USER_TERMINATED 0x13u

static void LeConnectPumpSlice(void) {
    if (HalCpuIsHypervisor()) {
        ProcessEvents();
    } else {
        ProcessEventsRealPc();
    }
    BluetoothHostPump();
    ServiceHidCompletions();
}

static int WaitCommandStatus(UINT16 Opcode, UINT32 TimeoutMs) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;

    while (ReadTsc() - Start < Need) {
        UINT8 Event[BLUETOOTH_EVENT_MAX];
        UINT32 Length = 0;
        UINT16 GotOpcode;

        LeConnectPumpSlice();
        while (BluetoothHostEventTake(Event, BLUETOOTH_EVENT_MAX, &Length)) {
            if (Length >= 6u && Event[0] == HCI_EVENT_COMMAND_STATUS) {
                GotOpcode = (UINT16)(Event[4] | ((UINT16)Event[5] << 8));
                if (GotOpcode == Opcode) {
                    if (Event[2] != 0u) {
                        BootLogHex("bt: le-conn CS status=", Event[2], 2);
                        return -1;
                    }
                    return 0;
                }
            }
            Length = 0;
        }
        StallMilliseconds(LE_CONNECT_SLICE_MS);
    }
    BootLogHex("bt: le-conn CS timeout op=", Opcode, 4);
    return -1;
}

static int WaitDisconnectionComplete(UINT16 Handle, UINT32 TimeoutMs) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;

    while (ReadTsc() - Start < Need) {
        UINT8 Event[BLUETOOTH_EVENT_MAX];
        UINT32 Length = 0;

        LeConnectPumpSlice();
        while (BluetoothHostEventTake(Event, BLUETOOTH_EVENT_MAX, &Length)) {
            UINT16 Got;

            if (Length >= 6u && Event[0] == HCI_EVENT_DISCONNECTION_COMPLETE) {
                Got = (UINT16)(Event[3] | ((UINT16)Event[4] << 8));
                Got = (UINT16)(Got & 0x0FFFu);
                if (Got == Handle) {
                    return Event[2] == 0u ? 0 : -1;
                }
            }
            Length = 0;
        }
        StallMilliseconds(LE_CONNECT_SLICE_MS);
    }
    BootLog("bt: le disconnect timeout\n");
    return -1;
}

void BluetoothLeAbortPending(void) {
    if (BluetoothLeIsConnected() != 0) {
        (void)XhciBluetoothLeDisconnect();
    }
    /* Address 建连中的 Cancel；无 pending 时为空操作 */
    BluetoothLeConnectAddressAbortPending();
}

int XhciBluetoothLeDisconnect(void) {
    UINT8 Params[3];
    UINT16 Handle = 0;

    if (BluetoothLeConnectionHandleGet(&Handle) < 0) {
        return 0;
    }
    if (!gBluetoothReady || !BluetoothHostPumpIsActive()) {
        BluetoothLeConnectionStateClear();
        return -1;
    }
    Params[0] = (UINT8)(Handle & 0xFFu);
    Params[1] = (UINT8)((Handle >> 8) & 0x0Fu);
    Params[2] = HCI_REASON_REMOTE_USER_TERMINATED;
    BluetoothHostEventQueueFlush();
    if (BluetoothHciSendRaw(HCI_OPCODE_DISCONNECT, Params, 3) < 0) {
        BootLog("bt: le Disconnect send fail\n");
        return -1;
    }
    if (WaitCommandStatus(HCI_OPCODE_DISCONNECT, 2000u) < 0) {
        return -1;
    }
    if (WaitDisconnectionComplete(Handle, LE_DISCONNECT_DEFAULT_MS) < 0) {
        BluetoothRequeueHid();
        return -1;
    }
    BluetoothLeConnectionStateClear();
    BootLog("bt: le disconnected\n");
    BluetoothRequeueHid();
    return 0;
}

int XhciBluetoothLeConnect(UINTN OneBasedIndex, UINT32 TimeoutMs) {
    UINT8 Address[6];
    UINT8 AddressType = 0;

    if (!gBluetoothReady || !BluetoothHostPumpIsActive()) {
        BootLog("bt: le connect need host pump\n");
        return -1;
    }
    if (BluetoothLeScanEntryGet(OneBasedIndex, Address, &AddressType) < 0) {
        BootLog("bt: le connect bad index (lescan first)\n");
        return -1;
    }
    return XhciBluetoothLeConnectAddress(Address, AddressType, TimeoutMs);
}
