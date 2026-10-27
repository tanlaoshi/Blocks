/*
 * Scan.c — 经典 Inquiry（btx-4）
 *
 * 标准 Inquiry Result（Mode 0）；名靠 RNR + 跨次缓存（ScanName.c）。
 * 取消后须抽干 Inquiry Complete，否则下次 Command_Status 必败。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"
#include "HalBluetooth.h"

#define BLUETOOTH_SCAN_CAP 16u
#define INQUIRY_SLICE_MS 200u
#define INQUIRY_DEFAULT_MS 8000u
#define DRAIN_MS 2000u

HAL_BLUETOOTH_SCAN_ENTRY gScanTable[BLUETOOTH_SCAN_CAP];
UINT8 gScanPageMode[BLUETOOTH_SCAN_CAP];
UINT16 gScanClockOffset[BLUETOOTH_SCAN_CAP];
UINTN gScanCount;

void BluetoothScanCacheSeed(UINTN Index);
void BluetoothScanFetchRemoteNames(void);

static int AddressEqual(const UINT8 A[6], const UINT8 B[6]) {
    UINT32 I;

    for (I = 0; I < 6u; I++) {
        if (A[I] != B[I]) {
            return 0;
        }
    }
    return 1;
}

static int FindOrAdd(const UINT8 Addr[6], UINT8 PageMode, UINT16 ClockOffset,
                     const UINT8 ClassOfDevice[3]) {
    UINTN I;
    UINT32 B;

    for (I = 0; I < gScanCount; I++) {
        if (AddressEqual(gScanTable[I].Address, Addr)) {
            gScanPageMode[I] = PageMode;
            gScanClockOffset[I] = ClockOffset;
            return (int)I;
        }
    }
    if (gScanCount >= BLUETOOTH_SCAN_CAP) {
        return -1;
    }
    I = gScanCount++;
    for (B = 0; B < 6u; B++) {
        gScanTable[I].Address[B] = Addr[B];
    }
    for (B = 0; B < 3u; B++) {
        gScanTable[I].ClassOfDevice[B] = ClassOfDevice ? ClassOfDevice[B] : 0;
    }
    gScanTable[I].Name[0] = 0;
    gScanPageMode[I] = PageMode;
    gScanClockOffset[I] = ClockOffset;
    BluetoothScanCacheSeed(I);
    return (int)I;
}

static void ParseInquiryResult(void) {
    UINT8 Num = gBluetoothEventBuffer[2];
    UINT8 Off = 3;
    UINT8 I;

    for (I = 0; I < Num; I++) {
        UINT8 Addr[6];
        UINT8 ClassOfDevice[3];
        UINT32 B;

        if ((UINT32)Off + 14u > (UINT32)gBluetoothEventBuffer[1] + 2u) {
            break;
        }
        for (B = 0; B < 6u; B++) {
            Addr[B] = gBluetoothEventBuffer[Off + B];
        }
        for (B = 0; B < 3u; B++) {
            ClassOfDevice[B] = gBluetoothEventBuffer[Off + 8u + B];
        }
        (void)FindOrAdd(Addr, gBluetoothEventBuffer[Off + 6],
                        (UINT16)(gBluetoothEventBuffer[Off + 11] |
                                 (gBluetoothEventBuffer[Off + 12] << 8)),
                        ClassOfDevice);
        Off = (UINT8)(Off + 14u);
    }
}

static void ParseInquiryResultRssi(void) {
    UINT8 Num = gBluetoothEventBuffer[2];
    UINT8 Off = 3;
    UINT8 I;

    for (I = 0; I < Num; I++) {
        UINT8 Addr[6];
        UINT8 ClassOfDevice[3];
        UINT32 B;

        if ((UINT32)Off + 15u > (UINT32)gBluetoothEventBuffer[1] + 2u) {
            break;
        }
        for (B = 0; B < 6u; B++) {
            Addr[B] = gBluetoothEventBuffer[Off + B];
        }
        for (B = 0; B < 3u; B++) {
            ClassOfDevice[B] = gBluetoothEventBuffer[Off + 8u + B];
        }
        (void)FindOrAdd(Addr, gBluetoothEventBuffer[Off + 6],
                        (UINT16)(gBluetoothEventBuffer[Off + 11] |
                                 (gBluetoothEventBuffer[Off + 12] << 8)),
                        ClassOfDevice);
        Off = (UINT8)(Off + 15u);
    }
}

static int WaitCommandStatus(UINT16 Opcode, UINT32 TimeoutMs) {
    UINT16 Got;

    if (BluetoothHciWaitArmed(TimeoutMs) < 0) {
        return -1;
    }
    if (gBluetoothEventBuffer[0] != HCI_EVENT_COMMAND_STATUS ||
        gBluetoothEventBuffer[1] < 4u || gBluetoothEventBuffer[2] != 0) {
        BootLogHex("Boot: Bluetooth Inquiry status evt=",
                   gBluetoothEventBuffer[0], 2);
        BootLogHex(" st=", gBluetoothEventBuffer[2], 2);
        return -1;
    }
    Got = (UINT16)(gBluetoothEventBuffer[4] |
                    (gBluetoothEventBuffer[5] << 8));
    return (Got == Opcode) ? 0 : -1;
}

/* 抽干至 Inquiry Complete；取消后 / 开扫前必须，否则下次 Inquiry 必败 */
static void DrainInquiryComplete(UINT32 TimeoutMs) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;

    while (ReadTsc() - Start < Need) {
        BluetoothHciArmEvent();
        if (BluetoothHciWaitArmed(INQUIRY_SLICE_MS) < 0) {
            continue;
        }
        if (gBluetoothEventBuffer[0] == HCI_EVENT_INQUIRY_COMPLETE) {
            return;
        }
        if (gBluetoothEventBuffer[0] == HCI_EVENT_INQUIRY_RESULT) {
            ParseInquiryResult();
        } else if (gBluetoothEventBuffer[0] == HCI_EVENT_INQUIRY_RESULT_RSSI) {
            ParseInquiryResultRssi();
        }
    }
}

static void CancelInquiry(void) {
    UINT8 Status = 0xFF;

    (void)BluetoothHciCommand(HCI_OPCODE_INQUIRY_CANCEL, 0, 0, &Status, 0, 0,
                              0);
    DrainInquiryComplete(DRAIN_MS);
}

static int RunInquiry(UINT32 TimeoutMs) {
    UINT8 Params[5];
    UINT64 Start;
    UINT64 Need;
    int Done = 0;

    /* GIAC；Length 单位 1.28s；Num_Responses=0 */
    Params[0] = 0x33;
    Params[1] = 0x8B;
    Params[2] = 0x9E;
    Params[3] = (TimeoutMs < 3000u) ? 0x03 : 0x06;
    Params[4] = 0x00;

    BluetoothHciArmEvent();
    if (BluetoothHciSendRaw(HCI_OPCODE_INQUIRY, Params, 5) < 0) {
        BootLog("Boot: Bluetooth Inquiry send fail\n");
        BluetoothRequeueHid();
        return -1;
    }
    if (WaitCommandStatus(HCI_OPCODE_INQUIRY, 2000u) < 0) {
        BootLog("Boot: Bluetooth Inquiry Command_Status fail\n");
        BluetoothRequeueHid();
        DrainInquiryComplete(DRAIN_MS);
        return -1;
    }

    Start = ReadTsc();
    Need = (UINT64)TimeoutMs * 3000000ULL;
    while (!Done) {
        BluetoothHciArmEvent();
        if (BluetoothHciWaitArmed(INQUIRY_SLICE_MS) < 0) {
            if (ReadTsc() - Start >= Need) {
                CancelInquiry();
                Done = 1;
            }
            continue;
        }
        if (gBluetoothEventBuffer[0] == HCI_EVENT_INQUIRY_RESULT) {
            ParseInquiryResult();
        } else if (gBluetoothEventBuffer[0] == HCI_EVENT_INQUIRY_RESULT_RSSI) {
            ParseInquiryResultRssi();
        } else if (gBluetoothEventBuffer[0] == HCI_EVENT_INQUIRY_COMPLETE) {
            Done = 1;
        }
        if (!Done && ReadTsc() - Start >= Need) {
            CancelInquiry();
            Done = 1;
        }
    }
    BluetoothRequeueHid();
    return 0;
}

int XhciBluetoothScan(HAL_BLUETOOTH_SCAN_ENTRY *Out, UINTN OutMax,
                      UINT32 TimeoutMs) {
    UINTN I;
    UINTN N;

    if (Out == 0 || OutMax == 0 || !gBluetoothReady) {
        return -1;
    }
    gScanCount = 0;
    ZeroMemory(gScanTable, sizeof(gScanTable));
    if (TimeoutMs == 0) {
        TimeoutMs = INQUIRY_DEFAULT_MS;
    }
    if (RunInquiry(TimeoutMs) < 0) {
        return -1;
    }
    BluetoothScanFetchRemoteNames();
    N = gScanCount;
    if (N > OutMax) {
        N = OutMax;
    }
    for (I = 0; I < N; I++) {
        Out[I] = gScanTable[I];
    }
    return (int)N;
}
