/*
 * Bluetooth.c — 壳 bt / bt info（btx-3）
 *
 * 演示板载地址与本地名；未 Ready 明示。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalBluetooth.h"

static void WriteHexByte(UINT8 Value) {
    static const char Digits[] = "0123456789ABCDEF";
    char Hex[3];

    /* 人读 BD_ADDR：两位十六进制，无 0x 前缀 */
    Hex[0] = Digits[(Value >> 4) & 0x0Fu];
    Hex[1] = Digits[Value & 0x0Fu];
    Hex[2] = 0;
    gConsole.Write(Hex);
}

static void CommandBluetooth(int Argc, char **Argv) {
    UINT8 Addr[HAL_BLUETOOTH_ADDRESS_LENGTH];
    char Name[HAL_BLUETOOTH_NAME_MAX];
    int I;

    (void)Argv;
    if (Argc >= 2) {
        /* 仅 info；其它子命令留给 btx-4 scan */
    }
    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready (no HCI / Reset / BD_ADDR yet)\n");
        return;
    }
    if (HalBluetoothGetDeviceAddress(Addr) != 0) {
        gConsole.Write("bt: addr fail\n");
        return;
    }
    Name[0] = 0;
    (void)HalBluetoothGetLocalName(Name, sizeof(Name));
    gConsole.Write("bt: ready name=");
    gConsole.Write(Name[0] ? Name : "(none)");
    gConsole.Write(" addr=");
    for (I = 5; I >= 0; I--) {
        WriteHexByte(Addr[I]);
        if (I > 0) {
            gConsole.Write(":");
        }
    }
    gConsole.Write("\n");
}

void ShellCommandsBluetoothRegister(void) {
    gConsole.Register("bt", "bt / bt info — addr + local name", CommandBluetooth);
}
