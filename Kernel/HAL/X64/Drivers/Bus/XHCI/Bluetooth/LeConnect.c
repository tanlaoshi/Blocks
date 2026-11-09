/* LeConnect.c — LE Create Connection / Disconnect；鼠优先切片 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"
#define HCI_LE_SUBEVENT_CONNECTION_COMPLETE 0x01u
#define LE_CONNECT_SLICE_MS 50u
#define LE_CONNECT_DEFAULT_MS 15000u
#define LE_DISCONNECT_DEFAULT_MS 5000u
#define HCI_REASON_REMOTE_USER_TERMINATED 0x13u
static int sLeCreatePending;
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
static int ParseConnectionComplete(const UINT8 *Event, UINT32 Length,
                                   UINT16 *HandleOut, UINT8 *StatusOut,
                                   UINT8 *PeerTypeOut, UINT8 PeerAddressOut[6]) {
    UINT16 Handle;
    UINT32 Byte;
    /* 0x3E plen sub=01 status handle[2] role peer_type addr[6] … */
    if (Length < 14u || Event[0] != HCI_EVENT_LE_META ||
        Event[2] != HCI_LE_SUBEVENT_CONNECTION_COMPLETE) {
        return 0;
    }
    if (StatusOut != 0) {
        *StatusOut = Event[3];
    }
    Handle = (UINT16)(Event[4] | ((UINT16)Event[5] << 8));
    Handle = (UINT16)(Handle & 0x0FFFu);
    if (HandleOut != 0) {
        *HandleOut = Handle;
    }
    if (PeerTypeOut != 0) {
        *PeerTypeOut = Event[7];
    }
    if (PeerAddressOut != 0) {
        for (Byte = 0; Byte < 6u; Byte++) {
            PeerAddressOut[Byte] = Event[8u + Byte];
        }
    }
    return 1;
}
static int WaitConnectionComplete(UINT32 TimeoutMs, UINT16 *HandleOut,
                                  UINT8 *PeerTypeOut, UINT8 PeerAddressOut[6]) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;
    while (ReadTsc() - Start < Need) {
        UINT8 Event[BLUETOOTH_EVENT_MAX];
        UINT32 Length = 0;
        LeConnectPumpSlice();
        while (BluetoothHostEventTake(Event, BLUETOOTH_EVENT_MAX, &Length)) {
            UINT8 Status = 0xFFu;
            UINT16 Handle = 0;
            if (ParseConnectionComplete(Event, Length, &Handle, &Status,
                                        PeerTypeOut, PeerAddressOut)) {
                if (Status != 0u) {
                    BootLogHex("bt: le Connection Complete status=", Status, 2);
                    return -1;
                }
                if (HandleOut != 0) {
                    *HandleOut = Handle;
                }
                return 0;
            }
            Length = 0;
        }
        StallMilliseconds(LE_CONNECT_SLICE_MS);
    }
    BootLog("bt: le Connection Complete timeout\n");
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
static int StopLeScanQuiet(void) {
    UINT8 Params[2];
    UINT8 Status = 0xFFu;
    Params[0] = 0;
    Params[1] = 0;
    (void)BluetoothHciCommand(HCI_OPCODE_LE_SET_SCAN_ENABLE, Params, 2, &Status,
                              0, 0, 0);
    return 0;
}
static void DrainConnectionComplete(UINT32 TimeoutMs) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;
    while (ReadTsc() - Start < Need) {
        UINT8 Event[BLUETOOTH_EVENT_MAX];
        UINT32 Length = 0;
        LeConnectPumpSlice();
        while (BluetoothHostEventTake(Event, BLUETOOTH_EVENT_MAX, &Length)) {
            UINT8 Status = 0xFFu;
            UINT16 Handle = 0;
            if (ParseConnectionComplete(Event, Length, &Handle, &Status, 0,
                                        0)) {
                (void)Handle;
                (void)Status;
                return;
            }
            Length = 0;
        }
        StallMilliseconds(LE_CONNECT_SLICE_MS);
    }
}
static int CancelCreateConnection(void) {
    UINT8 Status = 0xFFu;
    if (sLeCreatePending == 0) {
        return 0;
    }
    (void)BluetoothHciCommand(HCI_OPCODE_LE_CREATE_CONNECTION_CANCEL, 0, 0,
                              &Status, 0, 0, 0);
    DrainConnectionComplete(2000u);
    sLeCreatePending = 0;
    return 0;
}
void BluetoothLeAbortPending(void) {
    if (BluetoothLeIsConnected() != 0) {
        (void)XhciBluetoothLeDisconnect();
    }
    /* 无未完成 Create 时勿发 Cancel——Intel 上易搅乱后续 LE 命令 */
    (void)CancelCreateConnection();
}
static int SendCreateConnection(const UINT8 Address[6], UINT8 AddressType) {
    UINT8 Params[25];
    UINT32 Byte;
    /* Scan Interval/Window 0x0060/0x0030；Filter=0 用 Peer 地址 */
    Params[0] = 0x60u;
    Params[1] = 0x00u;
    Params[2] = 0x30u;
    Params[3] = 0x00u;
    Params[4] = 0x00u;
    Params[5] = AddressType;
    for (Byte = 0; Byte < 6u; Byte++) {
        Params[6u + Byte] = Address[Byte];
    }
    Params[12] = 0x00u; /* own public */
    /* Conn interval 7.5–15ms；latency 0；supervision 5s */
    Params[13] = 0x06u;
    Params[14] = 0x00u;
    Params[15] = 0x0Cu;
    Params[16] = 0x00u;
    Params[17] = 0x00u;
    Params[18] = 0x00u;
    Params[19] = 0xF4u;
    Params[20] = 0x01u;
    Params[21] = 0x00u;
    Params[22] = 0x00u;
    Params[23] = 0x00u;
    Params[24] = 0x00u;
    BluetoothHostEventQueueFlush();
    if (BluetoothHciSendRaw(HCI_OPCODE_LE_CREATE_CONNECTION, Params, 25) < 0) {
        BootLog("bt: le Create Connection send fail\n");
        return -1;
    }
    if (WaitCommandStatus(HCI_OPCODE_LE_CREATE_CONNECTION, 2000u) < 0) {
        sLeCreatePending = 0;
        return -1;
    }
    sLeCreatePending = 1;
    return 0;
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
    UINT8 PeerAddress[6];
    UINT8 PeerType = 0;
    UINT16 Handle = 0;
    UINT32 Byte;
    int Result = -1;
    if (!gBluetoothReady || !BluetoothHostPumpIsActive()) {
        BootLog("bt: le connect need host pump\n");
        return -1;
    }
    if (BluetoothLeScanEntryGet(OneBasedIndex, Address, &AddressType) < 0) {
        BootLog("bt: le connect bad index (lescan first)\n");
        return -1;
    }
    if (TimeoutMs == 0u) {
        TimeoutMs = LE_CONNECT_DEFAULT_MS;
    }
    if (BluetoothLeIsConnected() != 0) {
        (void)XhciBluetoothLeDisconnect();
    }
    if (BluetoothLeHostSupportApply() < 0) {
        BluetoothRequeueHid();
        return -1;
    }
    (void)StopLeScanQuiet();
    if (SendCreateConnection(Address, AddressType) < 0) {
        BluetoothRequeueHid();
        return -1;
    }
    for (Byte = 0; Byte < 6u; Byte++) {
        PeerAddress[Byte] = Address[Byte];
    }
    PeerType = AddressType;
    if (WaitConnectionComplete(TimeoutMs, &Handle, &PeerType, PeerAddress) <
        0) {
        (void)CancelCreateConnection();
        BluetoothRequeueHid();
        return -1;
    }
    sLeCreatePending = 0;
    BluetoothLeConnectionStateSet(Handle, PeerAddress, PeerType);
    BootLogHex("bt: le connected handle=", Handle, 4);
    BootLog("\n");
    Result = 0;
    BluetoothRequeueHid();
    return Result;
}
