/* LeScan.c — LE 扫描；名 AD 0x08/0x09 或 Logitech 0x0131 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"
#include "HalBluetooth.h"
#define BLUETOOTH_LE_SCAN_CAP 16u
#define LE_SCAN_SLICE_MS 50u
#define LE_SCAN_DEFAULT_MS 8000u
#define HCI_LE_SUBEVENT_ADVERTISING_REPORT 0x02u
#define HCI_AD_TYPE_NAME_SHORT 0x08u
#define HCI_AD_TYPE_NAME_COMPLETE 0x09u
#define HCI_AD_TYPE_MANUFACTURER 0xFFu
#define LOGITECH_COMPANY_ID 0x0131u
static HAL_BLUETOOTH_SCAN_ENTRY sLeScanTable[BLUETOOTH_LE_SCAN_CAP];
static UINT8 sLeScanAddressType[BLUETOOTH_LE_SCAN_CAP];
static UINTN sLeScanCount;
int BluetoothLeScanEntryGet(UINTN OneBasedIndex, UINT8 Address[6],
                            UINT8 *AddressTypeOut) {
    UINTN Index;
    UINT32 Byte;
    if (OneBasedIndex < 1u || OneBasedIndex > sLeScanCount || Address == 0) {
        return -1;
    }
    Index = OneBasedIndex - 1u;
    for (Byte = 0; Byte < 6u; Byte++) {
        Address[Byte] = sLeScanTable[Index].Address[Byte];
    }
    if (AddressTypeOut != 0) {
        *AddressTypeOut = sLeScanAddressType[Index];
    }
    return 0;
}
static int AddressEqual(const UINT8 A[6], const UINT8 B[6]) {
    UINT32 Index;
    for (Index = 0; Index < 6u; Index++) {
        if (A[Index] != B[Index]) {
            return 0;
        }
    }
    return 1;
}
static void CopyName(char *Out, UINTN OutMax, const UINT8 *Data, UINT8 Length) {
    UINTN Index;
    UINTN Cap;
    if (Out == 0 || OutMax < 1u) {
        return;
    }
    Cap = OutMax - 1u;
    if ((UINTN)Length < Cap) {
        Cap = Length;
    }
    for (Index = 0; Index < Cap; Index++) {
        Out[Index] = (char)Data[Index];
    }
    Out[Index] = 0;
}
static void ParseAdvertisingData(HAL_BLUETOOTH_SCAN_ENTRY *Entry,
                                 const UINT8 *Data, UINT8 Length) {
    UINT8 Offset = 0;
    int HaveName = 0;
    int HaveLogitech = 0;
    while ((UINT32)Offset + 1u <= (UINT32)Length) {
        UINT8 FieldLength = Data[Offset];
        UINT8 Type;
        UINT8 PayloadLength;
        if (FieldLength == 0u) {
            break;
        }
        if ((UINT32)Offset + 1u + (UINT32)FieldLength > (UINT32)Length) {
            break;
        }
        Type = Data[Offset + 1u];
        PayloadLength = (UINT8)(FieldLength - 1u);
        if (Type == HCI_AD_TYPE_NAME_COMPLETE || Type == HCI_AD_TYPE_NAME_SHORT) {
            CopyName(Entry->Name, HAL_BLUETOOTH_NAME_MAX, &Data[Offset + 2u],
                     PayloadLength);
            HaveName = 1;
        } else if (Type == HCI_AD_TYPE_MANUFACTURER && PayloadLength >= 2u) {
            UINT16 Company = (UINT16)(Data[Offset + 2u] |
                                      ((UINT16)Data[Offset + 3u] << 8));
            if (Company == LOGITECH_COMPANY_ID) {
                HaveLogitech = 1;
            }
        }
        Offset = (UINT8)(Offset + 1u + FieldLength);
    }
    if (!HaveName && HaveLogitech) {
        CopyName(Entry->Name, HAL_BLUETOOTH_NAME_MAX, (const UINT8 *)"(Logitech)",
                 10);
    }
}
static int FindOrAddLe(const UINT8 Address[6], UINT8 AddressType,
                       const UINT8 *AdvData, UINT8 AdvLength) {
    UINTN Index;
    UINT32 Byte;
    for (Index = 0; Index < sLeScanCount; Index++) {
        if (AddressEqual(sLeScanTable[Index].Address, Address) &&
            sLeScanAddressType[Index] == AddressType) {
            if (sLeScanTable[Index].Name[0] == 0 && AdvData != 0 &&
                AdvLength > 0u) {
                ParseAdvertisingData(&sLeScanTable[Index], AdvData, AdvLength);
            }
            return (int)Index;
        }
    }
    if (sLeScanCount >= BLUETOOTH_LE_SCAN_CAP) {
        return -1;
    }
    Index = sLeScanCount++;
    for (Byte = 0; Byte < 6u; Byte++) {
        sLeScanTable[Index].Address[Byte] = Address[Byte];
    }
    sLeScanAddressType[Index] = AddressType;
    for (Byte = 0; Byte < 3u; Byte++) {
        sLeScanTable[Index].ClassOfDevice[Byte] = 0;
    }
    sLeScanTable[Index].Name[0] = 0;
    if (AdvData != 0 && AdvLength > 0u) {
        ParseAdvertisingData(&sLeScanTable[Index], AdvData, AdvLength);
    }
    return (int)Index;
}
static void ParseLeAdvertisingReport(const UINT8 *Event, UINT32 Length) {
    UINT8 Num;
    UINT8 Offset;
    UINT8 Index;
    /* Event[0]=0x3E Event[1]=plen Event[2]=subevent */
    if (Length < 4u || Event[0] != HCI_EVENT_LE_META ||
        Event[2] != HCI_LE_SUBEVENT_ADVERTISING_REPORT) {
        return;
    }
    Num = Event[3];
    Offset = 4;
    for (Index = 0; Index < Num; Index++) {
        UINT8 AddressType;
        UINT8 Address[6];
        UINT8 DataLength;
        UINT8 Byte;
        if ((UINT32)Offset + 8u > Length) {
            break;
        }
        /* Event_Type, Address_Type, Address[6] */
        AddressType = Event[Offset + 1u];
        for (Byte = 0; Byte < 6u; Byte++) {
            Address[Byte] = Event[Offset + 2u + Byte];
        }
        DataLength = Event[Offset + 8u];
        if ((UINT32)Offset + 9u + (UINT32)DataLength + 1u > Length) {
            break;
        }
        (void)FindOrAddLe(Address, AddressType, &Event[Offset + 9u],
                          DataLength);
        Offset = (UINT8)(Offset + 9u + DataLength + 1u); /* +RSSI */
    }
}
static int LeCommand(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen) {
    UINT8 Status = 0xFFu;
    if (BluetoothHciCommand(Opcode, Params, ParamLen, &Status, 0, 0, 0) < 0) {
        BluetoothLeLastErrorSet(Opcode, 0xFEu);
        return -1;
    }
    if (Status != 0) {
        BluetoothLeLastErrorSet(Opcode, Status);
        BootLogHex("bt: le cmd status=", Status, 2);
        BootLogHex(" op=", Opcode, 4);
        return -1;
    }
    return 0;
}
static int LeScanEnable(int On) {
    UINT8 Params[2];
    Params[0] = On ? 1u : 0u;
    /* 停扫时 Filter_Duplicates=0：未在扫时 0x01 易 0x0C */
    Params[1] = On ? 1u : 0u;
    return LeCommand(HCI_OPCODE_LE_SET_SCAN_ENABLE, Params, 2);
}
static int LeScanEnsureStopped(void) {
    int Attempt;
    for (Attempt = 0; Attempt < 2; Attempt++) {
        UINT8 Status = 0xFFu;
        UINT8 Params[2];
        Params[0] = 0;
        Params[1] = 0;
        if (BluetoothHciCommand(HCI_OPCODE_LE_SET_SCAN_ENABLE, Params, 2,
                                &Status, 0, 0, 0) == 0) {
            return 0;
        }
        BluetoothLeLastErrorSet(HCI_OPCODE_LE_SET_SCAN_ENABLE, 0xFEu);
        StallMilliseconds(50);
    }
    return -1;
}
static int LeScanPrepare(void) {
    UINT8 Params[7];
    static int sHostSupportApplied;
    if (!sHostSupportApplied) {
        if (BluetoothLeHostSupportApply() < 0) {
            return -1;
        }
        sHostSupportApplied = 1;
    }
    if (LeScanEnsureStopped() < 0) {
        return -1;
    }
    /* Active 0x0060/0x0030；拒则 Passive */
    Params[0] = 0x01u;
    Params[1] = 0x60u;
    Params[2] = 0x00u;
    Params[3] = 0x30u;
    Params[4] = 0x00u;
    Params[5] = 0x00u;
    Params[6] = 0x00u;
    if (LeCommand(HCI_OPCODE_LE_SET_SCAN_PARAMETERS, Params, 7) == 0) {
        return 0;
    }
    Params[0] = 0x00u;
    return LeCommand(HCI_OPCODE_LE_SET_SCAN_PARAMETERS, Params, 7);
}
static void LeScanPumpSlice(void) {
    UINT8 Event[BLUETOOTH_EVENT_MAX];
    UINT32 Length = 0;
    if (HalCpuIsHypervisor()) {
        ProcessEvents();
    } else {
        ProcessEventsRealPc();
    }
    BluetoothHostPump();
    ServiceHidCompletions();
    while (BluetoothHostEventTake(Event, BLUETOOTH_EVENT_MAX, &Length)) {
        ParseLeAdvertisingReport(Event, Length);
        Length = 0;
    }
}
int XhciBluetoothLeScan(HAL_BLUETOOTH_SCAN_ENTRY *Out, UINTN OutMax,
                        UINT32 TimeoutMs) {
    UINT64 Start;
    UINT64 Need;
    UINTN Index;
    UINTN CopyCount;
    UINT32 Byte;
    if (!gBluetoothReady || Out == 0 || OutMax == 0u ||
        !BluetoothHostPumpIsActive()) {
        if (gBluetoothReady && BluetoothHostPumpIsActive() == 0) {
            BootLog("bt: lescan need host pump\n");
        }
        return -1;
    }
    if (TimeoutMs == 0u) {
        TimeoutMs = LE_SCAN_DEFAULT_MS;
    }
    sLeScanCount = 0;
    BluetoothLeAbortPending();
    BluetoothHostEventQueueFlush();
    BluetoothLeLastErrorSet(0, 0);
    if (LeScanPrepare() < 0) {
        BluetoothRequeueHid();
        return -1;
    }
    if (LeScanEnable(1) < 0) {
        BluetoothRequeueHid();
        return -1;
    }
    Start = ReadTsc();
    Need = (UINT64)TimeoutMs * 3000000ULL;
    while (ReadTsc() - Start < Need) {
        LeScanPumpSlice();
        StallMilliseconds(LE_SCAN_SLICE_MS);
    }
    (void)LeScanEnable(0);
    Start = ReadTsc();
    Need = 500ULL * 3000000ULL;
    while (ReadTsc() - Start < Need) {
        LeScanPumpSlice();
    }
    CopyCount = sLeScanCount;
    if (CopyCount > OutMax) {
        CopyCount = OutMax;
    }
    for (Index = 0; Index < CopyCount; Index++) {
        for (Byte = 0; Byte < 6u; Byte++) {
            Out[Index].Address[Byte] = sLeScanTable[Index].Address[Byte];
        }
        for (Byte = 0; Byte < 3u; Byte++) {
            Out[Index].ClassOfDevice[Byte] =
                sLeScanTable[Index].ClassOfDevice[Byte];
        }
        {
            UINTN NameIndex = 0;
            while (NameIndex + 1u < HAL_BLUETOOTH_NAME_MAX &&
                   sLeScanTable[Index].Name[NameIndex] != 0) {
                Out[Index].Name[NameIndex] = sLeScanTable[Index].Name[NameIndex];
                NameIndex++;
            }
            Out[Index].Name[NameIndex] = 0;
        }
    }
    BluetoothRequeueHid();
    return (int)CopyCount;
}
