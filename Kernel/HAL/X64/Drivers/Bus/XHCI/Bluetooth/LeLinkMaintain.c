/*
 * LeLinkMaintain.c — 空闲保活：收 Disconnection；应答 Conn Param Update（PR-BTM-8）
 *
 * Master 连上后常发 L2CAP/HCI 参数更新；不应答会 idle 掉线。
 * 铁律：ConsumeEvent/ConsumeAclIn 热路径禁止 AclOutSend/ControlTransfer——
 * 会重入 HostPump、双 Arm ACL IN，有线+蓝牙鼠一起死。只入队，泵尾再发。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"

#define L2CAP_CID_SIGNALING_LE 0x0005u
#define L2CAP_CODE_CONNECTION_PARAMETER_UPDATE_REQUEST 0x12u
#define L2CAP_CODE_CONNECTION_PARAMETER_UPDATE_RESPONSE 0x13u
#define L2CAP_CONNECTION_PARAMETER_UPDATE_ACCEPTED 0x0000u
#define HCI_OPCODE_LE_CONNECTION_UPDATE 0x2013u
#define HCI_OPCODE_LE_REMOTE_CONNECTION_PARAMETER_REQUEST_REPLY 0x2020u
#define HCI_LE_SUBEVENT_REMOTE_CONNECTION_PARAMETER_REQUEST 0x06u
#define HCI_SUPERVISION_TIMEOUT_20S 0x07D0u /* 2000×10ms */

#define MAINTAIN_KIND_NONE 0
#define MAINTAIN_KIND_L2CAP_CONNECTION_PARAMETER 1
#define MAINTAIN_KIND_REMOTE_PARAMETER_REPLY 2

typedef struct {
    int Kind;
    UINT16 Handle;
    UINT16 IntervalMin;
    UINT16 IntervalMax;
    UINT16 Latency;
    UINT16 Timeout;
    UINT8 L2capIdentifier;
} BLUETOOTH_LE_LINK_MAINTAIN_PENDING;

static BLUETOOTH_LE_LINK_MAINTAIN_PENDING sMaintainPending;
static int sMaintainServiceBusy;

static UINT16 ReadLittleEndian16(const UINT8 *Data) {
    return (UINT16)(Data[0] | ((UINT16)Data[1] << 8));
}

static void WriteLittleEndian16(UINT8 *Data, UINT16 Value) {
    Data[0] = (UINT8)(Value & 0xFFu);
    Data[1] = (UINT8)((Value >> 8) & 0xFFu);
}

static UINT16 ClampSupervisionTimeout(UINT16 Timeout, UINT16 IntervalMax,
                                      UINT16 Latency) {
    UINT32 Need;

    /*
     * Core：Timeout(10ms) > (1+Latency)×IntervalMax(1.25ms)×2 / 10ms
     *      = (1+Latency)×IntervalMax / 4
     */
    Need = ((UINT32)(1u + Latency) * (UINT32)IntervalMax + 3u) / 4u;
    if (Need < 10u) {
        Need = 10u;
    }
    if ((UINT32)Timeout <= Need) {
        Timeout = (UINT16)(Need + 1u);
    }
    if (Timeout < HCI_SUPERVISION_TIMEOUT_20S) {
        Timeout = HCI_SUPERVISION_TIMEOUT_20S;
    }
    if (Timeout > 0x0C80u) {
        Timeout = 0x0C80u;
    }
    return Timeout;
}

static void QueueMaintain(int Kind, UINT16 Handle, UINT16 IntervalMin,
                          UINT16 IntervalMax, UINT16 Latency, UINT16 Timeout,
                          UINT8 L2capIdentifier) {
    /* 后来的参数更新覆盖未发的；不断线期间只留最新一笔 */
    sMaintainPending.Kind = Kind;
    sMaintainPending.Handle = (UINT16)(Handle & 0x0FFFu);
    sMaintainPending.IntervalMin = IntervalMin;
    sMaintainPending.IntervalMax = IntervalMax;
    sMaintainPending.Latency = Latency;
    sMaintainPending.Timeout = Timeout;
    sMaintainPending.L2capIdentifier = L2capIdentifier;
}

static int SendLeConnectionUpdate(UINT16 Handle, UINT16 IntervalMin,
                                  UINT16 IntervalMax, UINT16 Latency,
                                  UINT16 Timeout) {
    UINT8 Params[14];

    Timeout = ClampSupervisionTimeout(Timeout, IntervalMax, Latency);
    WriteLittleEndian16(&Params[0], (UINT16)(Handle & 0x0FFFu));
    WriteLittleEndian16(&Params[2], IntervalMin);
    WriteLittleEndian16(&Params[4], IntervalMax);
    WriteLittleEndian16(&Params[6], Latency);
    WriteLittleEndian16(&Params[8], Timeout);
    WriteLittleEndian16(&Params[10], 0);
    WriteLittleEndian16(&Params[12], 0);
    if (BluetoothHciSendRaw(HCI_OPCODE_LE_CONNECTION_UPDATE, Params, 14) <
        0) {
        return -1;
    }
    BootLogHex("bt: le conn-update timeout=", Timeout, 4);
    return 0;
}

static int SendRemoteParameterRequestReply(UINT16 Handle, UINT16 IntervalMin,
                                           UINT16 IntervalMax, UINT16 Latency,
                                           UINT16 Timeout) {
    UINT8 Params[14];

    Timeout = ClampSupervisionTimeout(Timeout, IntervalMax, Latency);
    WriteLittleEndian16(&Params[0], (UINT16)(Handle & 0x0FFFu));
    WriteLittleEndian16(&Params[2], IntervalMin);
    WriteLittleEndian16(&Params[4], IntervalMax);
    WriteLittleEndian16(&Params[6], Latency);
    WriteLittleEndian16(&Params[8], Timeout);
    WriteLittleEndian16(&Params[10], 0);
    WriteLittleEndian16(&Params[12], 0);
    if (BluetoothHciSendRaw(
            HCI_OPCODE_LE_REMOTE_CONNECTION_PARAMETER_REQUEST_REPLY, Params,
            14) < 0) {
        return -1;
    }
    BootLog("bt: le remote-param reply\n");
    return 0;
}

static int SendL2capConnectionParameterAccept(UINT16 Handle,
                                              UINT8 Identifier) {
    UINT8 Response[10];

    WriteLittleEndian16(&Response[0], 6);
    WriteLittleEndian16(&Response[2], L2CAP_CID_SIGNALING_LE);
    Response[4] = L2CAP_CODE_CONNECTION_PARAMETER_UPDATE_RESPONSE;
    Response[5] = Identifier;
    WriteLittleEndian16(&Response[6], 2);
    WriteLittleEndian16(&Response[8], L2CAP_CONNECTION_PARAMETER_UPDATE_ACCEPTED);
    if (BluetoothAclOutSend(Handle, Response, 10) < 0) {
        return -1;
    }
    BootLog("bt: l2cap conn-param accept\n");
    return 0;
}

static void HandleDisconnectionComplete(const UINT8 *Event, UINT32 Length) {
    UINT16 Handle;
    UINT16 Ours = 0;
    UINT8 Reason;

    if (Event == 0 || Length < 6u ||
        Event[0] != HCI_EVENT_DISCONNECTION_COMPLETE) {
        return;
    }
    Handle = (UINT16)(Event[3] | ((UINT16)Event[4] << 8));
    Handle = (UINT16)(Handle & 0x0FFFu);
    Reason = Event[5];
    if (BluetoothLeConnectionHandleGet(&Ours) < 0 || Ours != Handle) {
        return;
    }
    BootLogHex("bt: le lost reason=", Reason, 2);
    sMaintainPending.Kind = MAINTAIN_KIND_NONE;
    BluetoothLeConnectionStateClear();
    BluetoothMouseSessionTeardown();
}

static void HandleRemoteConnectionParameterRequest(const UINT8 *Event,
                                                   UINT32 Length) {
    UINT16 Handle;
    UINT16 Ours = 0;

    /* 0x3E plen sub=06 handle[2] intMin[2] intMax[2] latency[2] timeout[2] */
    if (Event == 0 || Length < 13u || Event[0] != HCI_EVENT_LE_META ||
        Event[2] != HCI_LE_SUBEVENT_REMOTE_CONNECTION_PARAMETER_REQUEST) {
        return;
    }
    Handle = (UINT16)(Event[3] | ((UINT16)Event[4] << 8));
    Handle = (UINT16)(Handle & 0x0FFFu);
    if (BluetoothLeConnectionHandleGet(&Ours) < 0 || Ours != Handle) {
        return;
    }
    QueueMaintain(MAINTAIN_KIND_REMOTE_PARAMETER_REPLY, Handle,
                  ReadLittleEndian16(&Event[5]), ReadLittleEndian16(&Event[7]),
                  ReadLittleEndian16(&Event[9]), ReadLittleEndian16(&Event[11]),
                  0);
}

void BluetoothLeLinkEventDemux(const UINT8 *Event, UINT32 Length) {
    if (Event == 0 || Length < 2u) {
        return;
    }
    if (Event[0] == HCI_EVENT_DISCONNECTION_COMPLETE) {
        HandleDisconnectionComplete(Event, Length);
        return;
    }
    if (Event[0] == HCI_EVENT_LE_META) {
        HandleRemoteConnectionParameterRequest(Event, Length);
    }
}

int BluetoothLeLinkL2capSignalPush(const UINT8 *AclPayload, UINT16 Length) {
    UINT16 L2Length;
    UINT16 ChannelIdentifier;
    UINT8 Code;
    UINT8 Identifier;
    UINT16 Handle = 0;

    if (AclPayload == 0 || Length < 10u) {
        return 0;
    }
    L2Length = ReadLittleEndian16(&AclPayload[0]);
    ChannelIdentifier = ReadLittleEndian16(&AclPayload[2]);
    if (ChannelIdentifier != L2CAP_CID_SIGNALING_LE || L2Length < 6u ||
        (UINT16)(4u + L2Length) > Length) {
        return 0;
    }
    Code = AclPayload[4];
    Identifier = AclPayload[5];
    if (Code != L2CAP_CODE_CONNECTION_PARAMETER_UPDATE_REQUEST) {
        return 0;
    }
    if (L2Length < 12u || BluetoothLeConnectionHandleGet(&Handle) < 0) {
        return 1;
    }
    QueueMaintain(MAINTAIN_KIND_L2CAP_CONNECTION_PARAMETER, Handle,
                  ReadLittleEndian16(&AclPayload[8]),
                  ReadLittleEndian16(&AclPayload[10]),
                  ReadLittleEndian16(&AclPayload[12]),
                  ReadLittleEndian16(&AclPayload[14]), Identifier);
    return 1;
}

/*
 * 泵尾调用：Consume 已退出。可 AclOut / HCI Control；仍禁再进本函数。
 */
void BluetoothLeLinkMaintainService(void) {
    BLUETOOTH_LE_LINK_MAINTAIN_PENDING Job;

    if (sMaintainServiceBusy || sMaintainPending.Kind == MAINTAIN_KIND_NONE) {
        return;
    }
    if (EventIsExclusive()) {
        return;
    }
    Job = sMaintainPending;
    sMaintainPending.Kind = MAINTAIN_KIND_NONE;
    sMaintainServiceBusy = 1;
    if (Job.Kind == MAINTAIN_KIND_L2CAP_CONNECTION_PARAMETER) {
        if (SendL2capConnectionParameterAccept(Job.Handle,
                                               Job.L2capIdentifier) == 0) {
            (void)SendLeConnectionUpdate(Job.Handle, Job.IntervalMin,
                                         Job.IntervalMax, Job.Latency,
                                         Job.Timeout);
        }
    } else if (Job.Kind == MAINTAIN_KIND_REMOTE_PARAMETER_REPLY) {
        (void)SendRemoteParameterRequestReply(Job.Handle, Job.IntervalMin,
                                              Job.IntervalMax, Job.Latency,
                                              Job.Timeout);
    }
    sMaintainServiceBusy = 0;
}
