/*
 * BringUp.c — Pi 3B 蓝牙 bring-up（bt-pi-2）
 *
 * Docs/26 §3.3 / Docs/24 § bt-pi-2：
 *   UART0 → HCI_Reset → .hcd →（再 Reset）→ Read_BD_ADDR →
 *   Write_Local_Name("Blocks") → Ready（Reset+读址成功；名失败可回落）。
 * 判据串：UART0 up → HCI Reset ok → HCI BD_ADDR=… → Bluetooth ready
 */
#include "BluetoothPrivate.h"
#include "Hal.h"
#include "HalSerial.h"

static const char *sPiBluetoothDiagnosis = "not run";
static UINT32 sPiBluetoothAttemptMask;
static int sPiBluetoothReady;
static UINT8 sPiBluetoothAddress[PI_BT_ADDRESS_LENGTH];
static char sPiBluetoothLocalName[PI_BT_LOCAL_NAME_STORE];

const char *BoardBluetoothDiagnosis(void) {
    return sPiBluetoothDiagnosis;
}

UINT32 BoardBluetoothAttemptMask(void) {
    return sPiBluetoothAttemptMask;
}

int BoardBluetoothReady(void) {
    return sPiBluetoothReady;
}

int BoardBluetoothCopyDeviceAddress(UINT8 Out[PI_BT_ADDRESS_LENGTH]) {
    UINT32 Index;

    if (Out == 0 || !sPiBluetoothReady) {
        return -1;
    }
    for (Index = 0u; Index < PI_BT_ADDRESS_LENGTH; Index++) {
        Out[Index] = sPiBluetoothAddress[Index];
    }
    return 0;
}

int BoardBluetoothCopyLocalName(char *Out, UINTN NameMax) {
    UINTN Index;

    if (Out == 0 || NameMax < 1u || !sPiBluetoothReady) {
        return -1;
    }
    for (Index = 0u; Index + 1u < NameMax && sPiBluetoothLocalName[Index] != 0; Index++) {
        Out[Index] = sPiBluetoothLocalName[Index];
    }
    Out[Index] = 0;
    return 0;
}

static void PiBluetoothDrainReceive(void) {
    UINT8 Byte;
    UINT32 Guard = 64u;

    while (Guard-- > 0u && PiUartReadByte(&Byte, 1u) == 0) {
    }
}

static void PiBluetoothClearIdentity(void) {
    UINT32 Index;

    sPiBluetoothReady = 0;
    for (Index = 0u; Index < PI_BT_ADDRESS_LENGTH; Index++) {
        sPiBluetoothAddress[Index] = 0u;
    }
    sPiBluetoothLocalName[0] = 0;
}

static int PiBluetoothHciResetOnce(void) {
    UINT8 Status = 0xFFu;

    PiBluetoothDrainReceive();
    if (PiHciSendCommand(PI_HCI_OPCODE_RESET, 0, 0u, &Status, 0, 0u, 0) != 0) {
        return -1;
    }
    if (Status != 0u) {
        return -2;
    }
    return 0;
}

static void PiBluetoothLogAddress(void) {
    static const char Digits[] = "0123456789ABCDEF";
    char Line[80];
    int Position = 0;
    int ByteIndex;
    const char *Prefix = "Boot: Bluetooth HCI BD_ADDR=";
    const char *Cursor;

    for (Cursor = Prefix; *Cursor != 0 && Position < 60; Cursor++) {
        Line[Position++] = *Cursor;
    }
    /* HCI 线序 LSB 在前 → 人读高字节在前 */
    for (ByteIndex = 5; ByteIndex >= 0; ByteIndex--) {
        UINT8 Value = sPiBluetoothAddress[ByteIndex];

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

static int PiBluetoothAddressIsAllZero(void) {
    UINT32 Index;

    for (Index = 0u; Index < PI_BT_ADDRESS_LENGTH; Index++) {
        if (sPiBluetoothAddress[Index] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int PiBluetoothReadDeviceAddress(void) {
    UINT8 Status = 0xFFu;
    UINT8 Address[PI_BT_ADDRESS_LENGTH];
    UINT8 ReturnLength = 0u;
    UINT32 Index;

    if (PiHciSendCommand(PI_HCI_OPCODE_READ_BD_ADDR, 0, 0u, &Status, Address,
                         PI_BT_ADDRESS_LENGTH, &ReturnLength) != 0) {
        return -1;
    }
    if (Status != 0u || ReturnLength < PI_BT_ADDRESS_LENGTH) {
        return -2;
    }
    for (Index = 0u; Index < PI_BT_ADDRESS_LENGTH; Index++) {
        sPiBluetoothAddress[Index] = Address[Index];
    }
    if (PiBluetoothAddressIsAllZero()) {
        return -3;
    }
    return 0;
}

static void PiBluetoothStoreLocalNameFallback(void) {
    const char *Name = "Blocks";
    UINT32 Index;

    for (Index = 0u; Name[Index] != 0 && Index + 1u < PI_BT_LOCAL_NAME_STORE; Index++) {
        sPiBluetoothLocalName[Index] = Name[Index];
    }
    sPiBluetoothLocalName[Index] = 0;
}

static int PiBluetoothWriteLocalName(void) {
    UINT8 Name[PI_HCI_LOCAL_NAME_LENGTH];
    UINT8 Status = 0xFFu;
    const char *Source = "Blocks";
    UINT32 Index;

    for (Index = 0u; Index < PI_HCI_LOCAL_NAME_LENGTH; Index++) {
        Name[Index] = 0u;
    }
    for (Index = 0u; Source[Index] != 0 && Index < PI_HCI_LOCAL_NAME_LENGTH; Index++) {
        Name[Index] = (UINT8)Source[Index];
    }
    if (PiHciSendCommand(PI_HCI_OPCODE_WRITE_LOCAL_NAME, Name, PI_HCI_LOCAL_NAME_LENGTH,
                         &Status, 0, 0u, 0) != 0 ||
        Status != 0u) {
        return -1;
    }
    PiBluetoothStoreLocalNameFallback();
    return 0;
}

int BoardBluetoothBringUp(void) {
    PiBluetoothClearIdentity();
    sPiBluetoothAttemptMask = 0u;
    (void)PiBluetoothClock2Enable32k();
    (void)PiBluetoothPowerEnable();
    (void)PiUartTransportInitialize();
    HalSerialBootMarkForce("Boot: Bluetooth UART0 up\n");

    if (PiBluetoothHciResetOnce() != 0) {
        sPiBluetoothDiagnosis = "no response / Reset status";
        HalSerialBootMarkForce("Boot: Bluetooth HCI Reset failed\n");
        return -1;
    }
    sPiBluetoothDiagnosis = "HCI Reset ok";
    HalSerialBootMarkForce("Boot: Bluetooth HCI Reset ok\n");

    if (PiBluetoothFirmwareLoad() != 0) {
        sPiBluetoothDiagnosis = ".hcd fail";
        return -2;
    }

    /* Launch_RAM 后控制器重载；再 Reset 再读址（对表 btbcm_finalize 语义） */
    HalDelayMilliseconds(50u);
    if (PiBluetoothHciResetOnce() != 0) {
        sPiBluetoothDiagnosis = "post-fw Reset fail";
        HalSerialBootMarkForce("Boot: Bluetooth post-fw Reset fail\n");
        return -3;
    }

    if (PiBluetoothReadDeviceAddress() != 0) {
        sPiBluetoothDiagnosis = "Read_BD_ADDR fail";
        HalSerialBootMarkForce("Boot: Bluetooth Read_BD_ADDR fail\n");
        return -4;
    }
    PiBluetoothLogAddress();

    if (PiBluetoothWriteLocalName() != 0) {
        PiBluetoothStoreLocalNameFallback();
        HalSerialBootMarkForce("Boot: Bluetooth name fallback Blocks\n");
    }

    sPiBluetoothReady = 1;
    sPiBluetoothDiagnosis = "ready";
    HalSerialBootMarkForce("Boot: Bluetooth ready\n");
    return 0;
}

int BoardBluetoothSelfTest(void) {
    return BoardBluetoothBringUp();
}
