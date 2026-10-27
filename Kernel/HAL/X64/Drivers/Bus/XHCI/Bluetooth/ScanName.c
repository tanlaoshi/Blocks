/*
 * ScanName.c — Remote Name + 跨次缓存（btx-4 辅）
 *
 * Inquiry 只给地址；名靠 Remote_Name_Request。
 * 页超时加长 + Clock_Offset 无效位强制完整 page（课堂手机常见）。
 */
#include "XHCI/Bluetooth/Private.h"
#include "HalBluetooth.h"

#define BLUETOOTH_SCAN_CAP 16u
#define NAME_WAIT_MS 6000u

extern HAL_BLUETOOTH_SCAN_ENTRY gScanTable[BLUETOOTH_SCAN_CAP];
extern UINT8 gScanPageMode[BLUETOOTH_SCAN_CAP];
extern UINT16 gScanClockOffset[BLUETOOTH_SCAN_CAP];
extern UINTN gScanCount;

static UINT8 gNameCacheAddr[BLUETOOTH_SCAN_CAP][6];
static char gNameCacheText[BLUETOOTH_SCAN_CAP][HAL_BLUETOOTH_NAME_MAX];
static UINTN gNameCacheCount;
static int gPageTimeoutSet;

static int AddressEqual(const UINT8 A[6], const UINT8 B[6]) {
    UINT32 I;

    for (I = 0; I < 6u; I++) {
        if (A[I] != B[I]) {
            return 0;
        }
    }
    return 1;
}

void BluetoothScanCachePut(const UINT8 Addr[6], const char *Name) {
    UINTN I;
    UINT32 B;

    if (!Name || !Name[0]) {
        return;
    }
    for (I = 0; I < gNameCacheCount; I++) {
        if (AddressEqual(gNameCacheAddr[I], Addr)) {
            for (B = 0; B + 1u < HAL_BLUETOOTH_NAME_MAX && Name[B]; B++) {
                gNameCacheText[I][B] = Name[B];
            }
            gNameCacheText[I][B] = 0;
            return;
        }
    }
    if (gNameCacheCount >= BLUETOOTH_SCAN_CAP) {
        return;
    }
    I = gNameCacheCount++;
    for (B = 0; B < 6u; B++) {
        gNameCacheAddr[I][B] = Addr[B];
    }
    for (B = 0; B + 1u < HAL_BLUETOOTH_NAME_MAX && Name[B]; B++) {
        gNameCacheText[I][B] = Name[B];
    }
    gNameCacheText[I][B] = 0;
}

void BluetoothScanCacheSeed(UINTN Index) {
    UINTN I;
    UINT32 B;

    for (I = 0; I < gNameCacheCount; I++) {
        if (!AddressEqual(gNameCacheAddr[I], gScanTable[Index].Address)) {
            continue;
        }
        for (B = 0; B + 1u < HAL_BLUETOOTH_NAME_MAX && gNameCacheText[I][B];
             B++) {
            gScanTable[Index].Name[B] = gNameCacheText[I][B];
        }
        gScanTable[Index].Name[B] = 0;
        return;
    }
}

void BluetoothScanCopyName(UINTN Index, const UINT8 *Src, UINT8 MaxLen) {
    UINTN N = 0;

    if (gScanTable[Index].Name[0]) {
        return;
    }
    while (N + 1u < HAL_BLUETOOTH_NAME_MAX && N < MaxLen && Src[N]) {
        /* 可打印 ASCII；UTF-8 多字节也原样收（壳可显示） */
        gScanTable[Index].Name[N] = (char)Src[N];
        N++;
    }
    gScanTable[Index].Name[N] = 0;
    BluetoothScanCachePut(gScanTable[Index].Address, gScanTable[Index].Name);
}

void BluetoothScanParseEir(UINTN Index, const UINT8 *Eir, UINTN EirLen) {
    UINTN Off = 0;

    while (Off < EirLen) {
        UINT8 Len = Eir[Off];
        UINT8 Type;
        UINT8 DataLen;

        if (Len == 0) {
            break;
        }
        if (Off + 1u + Len > EirLen) {
            break;
        }
        Type = Eir[Off + 1u];
        DataLen = (UINT8)(Len - 1u);
        if ((Type == HCI_EIR_NAME_COMPLETE || Type == HCI_EIR_NAME_SHORT) &&
            DataLen > 0) {
            BluetoothScanCopyName(Index, &Eir[Off + 2u], DataLen);
            if (Type == HCI_EIR_NAME_COMPLETE) {
                return;
            }
        }
        Off += 1u + (UINTN)Len;
    }
}

static int WaitCommandStatus(UINT16 Opcode, UINT32 TimeoutMs) {
    UINT16 Got;

    if (BluetoothHciWaitArmed(TimeoutMs) < 0) {
        return -1;
    }
    if (gBluetoothEventBuffer[0] != HCI_EVENT_COMMAND_STATUS ||
        gBluetoothEventBuffer[1] < 4u || gBluetoothEventBuffer[2] != 0) {
        return -1;
    }
    Got = (UINT16)(gBluetoothEventBuffer[4] |
                    (gBluetoothEventBuffer[5] << 8));
    return (Got == Opcode) ? 0 : -1;
}

static void CancelRemoteName(const UINT8 Addr[6]) {
    UINT8 Status = 0xFF;

    (void)BluetoothHciCommand(HCI_OPCODE_REMOTE_NAME_CANCEL, Addr, 6, &Status, 0,
                              0, 0);
}

static void EnsurePageTimeout(void) {
    UINT8 Params[2];
    UINT8 Status = 0xFF;

    if (gPageTimeoutSet) {
        return;
    }
    /* 0x4000 × 0.625ms ≈ 10.24s；默认 0x2000 对课堂手机偏紧 */
    Params[0] = 0x00;
    Params[1] = 0x40;
    if (BluetoothHciCommand(HCI_OPCODE_WRITE_PAGE_TIMEOUT, Params, 2, &Status, 0,
                            0, 0) == 0 &&
        Status == 0) {
        gPageTimeoutSet = 1;
    }
}

static int TryRemoteName(UINTN Index, int UseInquiryClock) {
    UINT8 Params[10];
    UINT32 B;
    UINT8 Cap;
    UINT8 Want;

    for (B = 0; B < 6u; B++) {
        Params[B] = gScanTable[Index].Address[B];
    }
    Params[6] = gScanPageMode[Index];
    Params[7] = 0;
    if (UseInquiryClock) {
        Params[8] = (UINT8)(gScanClockOffset[Index] & 0xFFu);
        Params[9] = (UINT8)((gScanClockOffset[Index] >> 8) & 0xFFu);
    } else {
        /* bit15=0 → Clock_Offset 无效，控制器走完整 page */
        Params[8] = 0;
        Params[9] = 0;
    }

    BluetoothHciArmEvent();
    if (BluetoothHciSendRaw(HCI_OPCODE_REMOTE_NAME_REQUEST, Params, 10) < 0) {
        BluetoothRequeueHid();
        return 0;
    }
    if (WaitCommandStatus(HCI_OPCODE_REMOTE_NAME_REQUEST, 2000u) < 0) {
        BluetoothRequeueHid();
        return 0;
    }
    BluetoothHciArmEvent();
    if (BluetoothHciWaitArmed(NAME_WAIT_MS) < 0) {
        CancelRemoteName(gScanTable[Index].Address);
        BluetoothRequeueHid();
        return 0;
    }
    if (gBluetoothEventBuffer[0] != HCI_EVENT_REMOTE_NAME_COMPLETE ||
        gBluetoothEventBuffer[1] < 7u) {
        CancelRemoteName(gScanTable[Index].Address);
        BluetoothRequeueHid();
        return 0;
    }
    if (gBluetoothEventBuffer[2] != 0) {
        /* 页超时等：不必 Cancel（已 Complete） */
        BluetoothRequeueHid();
        return 0;
    }
    /* 缓冲只有 MPS：名最多 EVENT_MAX-9 字节 */
    Cap = (UINT8)(BLUETOOTH_EVENT_MAX > 9u ? (BLUETOOTH_EVENT_MAX - 9u) : 0);
    Want = (UINT8)(gBluetoothEventBuffer[1] - 7u);
    if (Want > Cap) {
        Want = Cap;
    }
    BluetoothScanCopyName(Index, &gBluetoothEventBuffer[9], Want);
    BluetoothRequeueHid();
    return gScanTable[Index].Name[0] ? 1 : 0;
}

void BluetoothScanFetchRemoteNames(void) {
    UINTN I;

    EnsurePageTimeout();
    for (I = 0; I < gScanCount; I++) {
        if (gScanTable[I].Name[0]) {
            continue;
        }
        /* Clock_Offset 无效 → 完整 page；失败再带 Inquiry 偏置试一次 */
        if (TryRemoteName(I, 0)) {
            continue;
        }
        if (!HalCpuIsHypervisor()) {
            StallMs(20);
        }
        (void)TryRemoteName(I, 1);
    }
}
