/*
 * BringUp.c — Duo S 蓝牙 bring-up（bt-duo-1）
 *
 * Docs/27 / Docs/24：UART4 → HCI_Reset → Read_BD_ADDR → Write_Local_Name("Blocks")
 * → Ready（当且仅当 Reset+读址成功）。
 * 本刀不改 Sdhci/Wifi*；不灌 fmacfwbt（若 CC 失败 → Ready=0 + 诊断，允许否证）。
 */
#include "BluetoothPrivate.h"
#include "Hal.h"
#include "HalSerial.h"

static const char *sDuoBluetoothDiagnosis = "not run";
static int sDuoBluetoothReady;
static UINT8 sDuoBluetoothAddress[DUO_BT_ADDRESS_LENGTH];
static char sDuoBluetoothLocalName[DUO_BT_LOCAL_NAME_STORE];

const char *BoardBluetoothDiagnosis(void) {
    return sDuoBluetoothDiagnosis;
}

int BoardBluetoothReady(void) {
    return sDuoBluetoothReady;
}

int BoardBluetoothCopyDeviceAddress(UINT8 Out[DUO_BT_ADDRESS_LENGTH]) {
    UINT32 Index;

    if (Out == 0 || !sDuoBluetoothReady) {
        return -1;
    }
    for (Index = 0u; Index < DUO_BT_ADDRESS_LENGTH; Index++) {
        Out[Index] = sDuoBluetoothAddress[Index];
    }
    return 0;
}

int BoardBluetoothCopyLocalName(char *Out, UINTN NameMax) {
    UINTN Index;

    if (Out == 0 || NameMax < 1u || !sDuoBluetoothReady) {
        return -1;
    }
    for (Index = 0u; Index + 1u < NameMax && sDuoBluetoothLocalName[Index] != 0; Index++) {
        Out[Index] = sDuoBluetoothLocalName[Index];
    }
    Out[Index] = 0;
    return 0;
}

static void DuoBluetoothClearIdentity(void) {
    UINT32 Index;

    sDuoBluetoothReady = 0;
    for (Index = 0u; Index < DUO_BT_ADDRESS_LENGTH; Index++) {
        sDuoBluetoothAddress[Index] = 0u;
    }
    sDuoBluetoothLocalName[0] = 0;
}

static void DuoBluetoothDrainReceive(void) {
    UINT8 Byte;
    UINT32 Guard = 64u;

    while (Guard-- > 0u && DuoBluetoothUartReadByte(&Byte, 1u) == 0) {
    }
}

static int DuoBluetoothHciResetOnce(void) {
    UINT8 Status = 0xFFu;

    DuoBluetoothDrainReceive();
    if (DuoBluetoothHciSendCommand(DUO_HCI_OPCODE_RESET, 0, 0u, &Status, 0, 0u, 0) != 0) {
        return -1;
    }
    if (Status != 0u) {
        return -2;
    }
    return 0;
}

static int DuoBluetoothAddressIsAllZero(void) {
    UINT32 Index;

    for (Index = 0u; Index < DUO_BT_ADDRESS_LENGTH; Index++) {
        if (sDuoBluetoothAddress[Index] != 0u) {
            return 0;
        }
    }
    return 1;
}

static void DuoBluetoothLogAddress(void) {
    static const char Digits[] = "0123456789ABCDEF";
    char Line[80];
    int Position = 0;
    int ByteIndex;
    const char *Prefix = "Boot: Bluetooth HCI BD_ADDR=";
    const char *Cursor;

    for (Cursor = Prefix; *Cursor != 0 && Position < 60; Cursor++) {
        Line[Position++] = *Cursor;
    }
    for (ByteIndex = 5; ByteIndex >= 0; ByteIndex--) {
        UINT8 Value = sDuoBluetoothAddress[ByteIndex];

        if (Position + 2 < 78) {
            Line[Position++] = Digits[(Value >> 4) & 0x0Fu];
            Line[Position++] = Digits[Value & 0x0Fu];
        }
        if (ByteIndex > 0 && Position < 78) {
            Line[Position++] = ':';
        }
    }
    if (Position < 79) {
        Line[Position++] = '\n';
    }
    Line[Position] = 0;
    HalSerialBootMarkForce(Line);
}

static int DuoBluetoothReadDeviceAddress(void) {
    UINT8 Status = 0xFFu;
    UINT8 Address[DUO_BT_ADDRESS_LENGTH];
    UINT8 ReturnLength = 0u;
    UINT32 Index;

    if (DuoBluetoothHciSendCommand(DUO_HCI_OPCODE_READ_BD_ADDR, 0, 0u, &Status, Address,
                                   DUO_BT_ADDRESS_LENGTH, &ReturnLength) != 0) {
        return -1;
    }
    if (Status != 0u || ReturnLength < DUO_BT_ADDRESS_LENGTH) {
        return -2;
    }
    for (Index = 0u; Index < DUO_BT_ADDRESS_LENGTH; Index++) {
        sDuoBluetoothAddress[Index] = Address[Index];
    }
    if (DuoBluetoothAddressIsAllZero()) {
        return -3;
    }
    return 0;
}

static void DuoBluetoothStoreLocalNameFallback(void) {
    const char *Name = "Blocks";
    UINT32 Index;

    for (Index = 0u; Name[Index] != 0 && Index + 1u < DUO_BT_LOCAL_NAME_STORE; Index++) {
        sDuoBluetoothLocalName[Index] = Name[Index];
    }
    sDuoBluetoothLocalName[Index] = 0;
}

static int DuoBluetoothWriteLocalName(void) {
    UINT8 Name[DUO_HCI_LOCAL_NAME_LENGTH];
    UINT8 Status = 0xFFu;
    const char *Source = "Blocks";
    UINT32 Index;

    for (Index = 0u; Index < DUO_HCI_LOCAL_NAME_LENGTH; Index++) {
        Name[Index] = 0u;
    }
    for (Index = 0u; Source[Index] != 0 && Index < DUO_HCI_LOCAL_NAME_LENGTH; Index++) {
        Name[Index] = (UINT8)Source[Index];
    }
    if (DuoBluetoothHciSendCommand(DUO_HCI_OPCODE_WRITE_LOCAL_NAME, Name,
                                   DUO_HCI_LOCAL_NAME_LENGTH, &Status, 0, 0u, 0) != 0 ||
        Status != 0u) {
        return -1;
    }
    DuoBluetoothStoreLocalNameFallback();
    return 0;
}

int BoardBluetoothBringUp(void) {
    DuoBluetoothClearIdentity();
    (void)DuoBluetoothUartTransportInitialize();
    HalSerialBootMarkForce("Boot: Bluetooth UART4 up\n");

    if (DuoBluetoothHciResetOnce() != 0) {
        sDuoBluetoothDiagnosis = "HCI Reset fail (fw/UART?)";
        HalSerialBootMarkForce("Boot: Bluetooth HCI Reset fail\n");
        return -1;
    }
    sDuoBluetoothDiagnosis = "HCI Reset ok";
    HalSerialBootMarkForce("Boot: Bluetooth HCI Reset ok\n");

    HalDelayMilliseconds(50u);
    if (DuoBluetoothReadDeviceAddress() != 0) {
        sDuoBluetoothDiagnosis = "Read_BD_ADDR fail";
        HalSerialBootMarkForce("Boot: Bluetooth Read_BD_ADDR fail\n");
        return -2;
    }
    DuoBluetoothLogAddress();

    if (DuoBluetoothWriteLocalName() != 0) {
        DuoBluetoothStoreLocalNameFallback();
        HalSerialBootMarkForce("Boot: Bluetooth name fallback Blocks\n");
    }

    sDuoBluetoothReady = 1;
    sDuoBluetoothDiagnosis = "ready";
    HalSerialBootMarkForce("Boot: Bluetooth ready\n");
    return 0;
}
