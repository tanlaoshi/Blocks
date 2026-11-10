/*
 * AttHidSubscribe.c — 读 Report Map；Protocol Mode=Report；订 Input Notify
 *
 * CCCD 常开；试订/等首包见 AttHidSubscribeTry.c（重连须对上 ValueHandle）。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"

#define ATT_REPORT_MAP_MAX 512u
#define ATT_STAGE_READ_MAP 0x0020u
#define ATT_STAGE_ENABLE_NOTIFY 0x0021u
#define ATT_DEFAULT_MS 20000u

static UINT8 sReportMap[ATT_REPORT_MAP_MAX];
static UINT16 sReportMapLength;

static void HexDumpPrefix(const char *Prefix, const UINT8 *Data, UINT16 Length,
                          UINT16 MaxShow) {
    UINT16 Index;
    UINT16 Show = Length;

    if (Show > MaxShow) {
        Show = MaxShow;
    }
    BootLog(Prefix ? Prefix : "bt: hex=");
    for (Index = 0; Index < Show; Index++) {
        BootLogHex(Index == 0u ? "" : " ", Data[Index], 2);
    }
    BootLog(Length > Show ? "...\n" : "\n");
}

static int SetProtocolModeReport(UINT16 ConnectionHandle, UINT32 TimeoutMs) {
    UINT16 ProtocolModeHandle = 0;
    UINT8 Mode[1];

    if (BluetoothAttHidDiscoveryGetProtocolMode(&ProtocolModeHandle) < 0 ||
        ProtocolModeHandle == 0u) {
        BootLog("bt: hid protocol-mode missing (continue)\n");
        return 0;
    }
    Mode[0] = 0x01u; /* Report Protocol Mode */
    if (BluetoothAttWriteRequest(ConnectionHandle, ProtocolModeHandle, Mode, 1,
                                 TimeoutMs, ATT_STAGE_ENABLE_NOTIFY) < 0) {
        if (BluetoothAttWriteCommand(ConnectionHandle, ProtocolModeHandle, Mode,
                                     1) < 0) {
            BootLog("bt: hid protocol-mode write fail\n");
            return -1;
        }
    }
    BootLog("bt: hid protocol-mode=report\n");
    BluetoothRequeueHid();
    return 0;
}

int XhciBluetoothAttHidSubscribe(UINT32 TimeoutMs) {
    UINT16 ConnectionHandle = 0;
    UINT16 ReportMapHandle = 0;

    if (TimeoutMs == 0u) {
        TimeoutMs = ATT_DEFAULT_MS;
    }
    BluetoothLeLastErrorSet(0, 0);
    sReportMapLength = 0;
    BluetoothHidInputNoteArmed(0, 0);
    BluetoothAttNotifyAcceptSet(0);
    BluetoothAttNotifyQueueFlush();
    if (BluetoothLeConnectionHandleGet(&ConnectionHandle) < 0) {
        BluetoothLeLastErrorSet(ATT_STAGE_READ_MAP, 0xFDu);
        return -1;
    }
    if (BluetoothAttHidDiscoveryGet(0, 0, &ReportMapHandle, 0, 0) < 0) {
        BootLog("bt: notify need hogp first\n");
        BluetoothLeLastErrorSet(ATT_STAGE_READ_MAP, 0xFDu);
        return -1;
    }
    if (!BluetoothHostPumpIsActive()) {
        return -1;
    }
    if (ReportMapHandle != 0u) {
        if (BluetoothAttReadLong(ConnectionHandle, ReportMapHandle, sReportMap,
                                 ATT_REPORT_MAP_MAX, &sReportMapLength,
                                 TimeoutMs, ATT_STAGE_READ_MAP) < 0) {
            BootLog("bt: report-map read fail\n");
            return -1;
        }
        BootLogHex("bt: report-map len=", sReportMapLength, 4);
        HexDumpPrefix("bt: report-map=", sReportMap, sReportMapLength, 16u);
    }
    if (SetProtocolModeReport(ConnectionHandle, TimeoutMs) < 0) {
        return -1;
    }
    if (BluetoothAttHidTryInputNotifications(ConnectionHandle, TimeoutMs) < 0) {
        BluetoothAttHidDisarmEnabledNotifications(ConnectionHandle);
        BluetoothAttHidRecoverWiredHid();
        return -1;
    }
    BluetoothAttNotifyAcceptSet(1);
    BluetoothAttHidRecoverWiredHid();
    BootLog("bt: notify ok (cccd armed)\n");
    return 0;
}

int BluetoothAttHidReportMapGet(const UINT8 **DataOut, UINT16 *LengthOut) {
    if (sReportMapLength == 0u) {
        return -1;
    }
    if (DataOut != 0) {
        *DataOut = sReportMap;
    }
    if (LengthOut != 0) {
        *LengthOut = sReportMapLength;
    }
    return 0;
}
