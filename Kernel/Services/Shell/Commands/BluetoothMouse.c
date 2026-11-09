/*
 * BluetoothMouse.c — 壳 bt mouse <n|addr>（PR-BTM-8）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalBluetooth.h"

static int ParseDecimal(const char *Text, UINTN *Out) {
    UINTN Value = 0;
    const char *Cursor = Text;

    if (Cursor == 0 || *Cursor == 0 || Out == 0) {
        return -1;
    }
    while (*Cursor) {
        if (*Cursor < '0' || *Cursor > '9') {
            return -1;
        }
        Value = Value * 10u + (UINTN)(*Cursor - '0');
        Cursor++;
    }
    *Out = Value;
    return 0;
}

static int HexNibble(char Character, UINT8 *Out) {
    if (Character >= '0' && Character <= '9') {
        *Out = (UINT8)(Character - '0');
        return 0;
    }
    if (Character >= 'a' && Character <= 'f') {
        *Out = (UINT8)(Character - 'a' + 10);
        return 0;
    }
    if (Character >= 'A' && Character <= 'F') {
        *Out = (UINT8)(Character - 'A' + 10);
        return 0;
    }
    return -1;
}

/* 显示序 AA:BB:..:FF → HCI 线序 Address[5]=AA … Address[0]=FF */
static int ParseAddress(const char *Text,
                        UINT8 Out[HAL_BLUETOOTH_ADDRESS_LENGTH]) {
    UINT32 DisplayIndex;
    const char *Cursor = Text;

    if (Text == 0 || Out == 0) {
        return -1;
    }
    for (DisplayIndex = 0; DisplayIndex < HAL_BLUETOOTH_ADDRESS_LENGTH;
         DisplayIndex++) {
        UINT8 High = 0;
        UINT8 Low = 0;

        if (HexNibble(Cursor[0], &High) < 0 || HexNibble(Cursor[1], &Low) < 0) {
            return -1;
        }
        Out[5u - DisplayIndex] = (UINT8)((High << 4) | Low);
        Cursor += 2;
        if (DisplayIndex + 1u < HAL_BLUETOOTH_ADDRESS_LENGTH) {
            if (*Cursor != ':' && *Cursor != '-') {
                return -1;
            }
            Cursor++;
        }
    }
    return (*Cursor == 0) ? 0 : -1;
}

static int ResolveLescanIndex(const char *Arg, UINTN *IndexOut) {
    UINT8 Want[HAL_BLUETOOTH_ADDRESS_LENGTH];

    if (Arg == 0 || IndexOut == 0) {
        return -1;
    }
    if (ParseDecimal(Arg, IndexOut) == 0 && *IndexOut >= 1u) {
        return 0;
    }
    if (ParseAddress(Arg, Want) < 0) {
        return -1;
    }
    return HalBluetoothLeScanFindAddress(Want, IndexOut);
}

void ShellBluetoothMouseConnect(int Argc, char **Argv) {
    UINTN Index = 0;
    UINT16 Stage = 0;
    UINT8 Status = 0;

    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    if (Argc < 3 || ResolveLescanIndex(Argv[2], &Index) != 0) {
        gConsole.Write("bt: mouse <n|aa:bb:cc:dd:ee:ff>  (from lescan)\n");
        return;
    }
    gConsole.Write("bt: mouse connect — keep moving MX Master\n");
    if (HalBluetoothMouseConnect(Index, 0) != 0) {
        HalBluetoothLeLastError(&Stage, &Status);
        gConsole.Write("bt: mouse fail stage=");
        gConsole.WriteHex32(Stage);
        gConsole.Write(" status=");
        gConsole.WriteHex32(Status);
        gConsole.Write("\n");
        return;
    }
    gConsole.Write("bt: mouse ok (drag / click; USB mouse still live)\n");
}
