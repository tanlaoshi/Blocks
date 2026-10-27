/*
 * Bluetooth.c — 壳 bt / info / scan / visible / hide（btx-3…5）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalBluetooth.h"

static void WriteHexByte(UINT8 Value) {
    static const char Digits[] = "0123456789ABCDEF";
    char Hex[3];

    Hex[0] = Digits[(Value >> 4) & 0x0Fu];
    Hex[1] = Digits[Value & 0x0Fu];
    Hex[2] = 0;
    gConsole.Write(Hex);
}

static void WriteAddress(const UINT8 Addr[HAL_BLUETOOTH_ADDRESS_LENGTH]) {
    int I;

    for (I = 5; I >= 0; I--) {
        WriteHexByte(Addr[I]);
        if (I > 0) {
            gConsole.Write(":");
        }
    }
}

static int ArgEqualIgnoreCase(const char *Arg, const char *Word) {
    const char *A = Word;
    const char *B = Arg;

    if (!B) {
        return 0;
    }
    while (*A && *B) {
        char Ca = *A;
        char Cb = *B;

        if (Ca >= 'A' && Ca <= 'Z') {
            Ca = (char)(Ca - 'A' + 'a');
        }
        if (Cb >= 'A' && Cb <= 'Z') {
            Cb = (char)(Cb - 'A' + 'a');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static void CommandBluetoothInfo(void) {
    UINT8 Addr[HAL_BLUETOOTH_ADDRESS_LENGTH];
    char Name[HAL_BLUETOOTH_NAME_MAX];

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
    WriteAddress(Addr);
    gConsole.Write(HalBluetoothDiscoverable() ? " visible\n" : " hidden\n");
}

static void CommandBluetoothScan(void) {
    HAL_BLUETOOTH_SCAN_ENTRY Table[HAL_BLUETOOTH_SCAN_MAX];
    int N;
    int I;

    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    gConsole.Write("bt: scanning (~8s)...\n");
    N = HalBluetoothScan(Table, HAL_BLUETOOTH_SCAN_MAX, 0);
    if (N < 0) {
        gConsole.Write("bt: scan fail\n");
        return;
    }
    if (N == 0) {
        gConsole.Write("bt: no devices (phone BT on?)\n");
        return;
    }
    for (I = 0; I < N; I++) {
        char Line[8];
        int D = I + 1;

        gConsole.Write("bt: ");
        Line[0] = (char)('0' + (D / 10));
        Line[1] = (char)('0' + (D % 10));
        Line[2] = 0;
        if (D < 10) {
            gConsole.Write(&Line[1]);
        } else {
            gConsole.Write(Line);
        }
        gConsole.Write(" ");
        WriteAddress(Table[I].Address);
        gConsole.Write(" ");
        gConsole.Write(Table[I].Name[0] ? Table[I].Name : "(no name)");
        gConsole.Write("\n");
    }
}

static void CommandBluetoothVisible(int On) {
    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    if (HalBluetoothSetDiscoverable(On) != 0) {
        gConsole.Write(On ? "bt: visible fail\n" : "bt: hide fail\n");
        return;
    }
    gConsole.Write(On ? "bt: visible (phone search Blocks)\n"
                      : "bt: hidden\n");
}

static void CommandBluetooth(int Argc, char **Argv) {
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "scan")) {
        CommandBluetoothScan();
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "visible")) {
        CommandBluetoothVisible(1);
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "hide")) {
        CommandBluetoothVisible(0);
        return;
    }
    CommandBluetoothInfo();
}

void ShellCommandsBluetoothRegister(void) {
    gConsole.Register("bt", "bt [info|scan|visible|hide]", CommandBluetooth);
}
