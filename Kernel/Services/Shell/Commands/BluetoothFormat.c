/*
 * BluetoothFormat.c — WriteHexByte / WriteAddress（自 Bluetooth.c 纯搬 · PR-NORM-16）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalBluetooth.h"

void WriteHexByte(UINT8 Value) {
    static const char Digits[] = "0123456789ABCDEF";
    char Hex[3];

    Hex[0] = Digits[(Value >> 4) & 0x0Fu];
    Hex[1] = Digits[Value & 0x0Fu];
    Hex[2] = 0;
    gConsole.Write(Hex);
}

void WriteAddress(const UINT8 Address[HAL_BLUETOOTH_ADDRESS_LENGTH]) {
    int Index;

    for (Index = 5; Index >= 0; Index--) {
        WriteHexByte(Address[Index]);
        if (Index > 0) {
            gConsole.Write(":");
        }
    }
}
