/*
 * BluetoothLe.c — 壳 lescan / connect / smp / hogp / notify（PR-BTM-3…7）
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

static void WriteAddress(const UINT8 Address[HAL_BLUETOOTH_ADDRESS_LENGTH]) {
    int Index;

    for (Index = 5; Index >= 0; Index--) {
        WriteHexByte(Address[Index]);
        if (Index > 0) {
            gConsole.Write(":");
        }
    }
}

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

void ShellBluetoothLeScan(void) {
    HAL_BLUETOOTH_SCAN_ENTRY Table[HAL_BLUETOOTH_SCAN_MAX];
    int Count;
    int Index;

    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    gConsole.Write("bt: lescan (~8s)...\n");
    Count = HalBluetoothLeScan(Table, HAL_BLUETOOTH_SCAN_MAX, 0);
    if (Count < 0) {
        UINT16 Opcode = 0;
        UINT8 Status = 0;

        HalBluetoothLeLastError(&Opcode, &Status);
        gConsole.Write("bt: lescan fail status=");
        WriteHexByte(Status);
        gConsole.Write(" op=");
        WriteHexByte((UINT8)((Opcode >> 8) & 0xFFu));
        WriteHexByte((UINT8)(Opcode & 0xFFu));
        gConsole.Write("\n");
        return;
    }
    if (Count == 0) {
        gConsole.Write("bt: lescan no devices (Master pairing mode?)\n");
        return;
    }
    for (Index = 0; Index < Count; Index++) {
        char Line[8];
        int Display = Index + 1;

        gConsole.Write("bt: ");
        Line[0] = (char)('0' + (Display / 10));
        Line[1] = (char)('0' + (Display % 10));
        Line[2] = 0;
        if (Display < 10) {
            gConsole.Write(&Line[1]);
        } else {
            gConsole.Write(Line);
        }
        gConsole.Write(" ");
        WriteAddress(Table[Index].Address);
        gConsole.Write(" ");
        gConsole.Write(Table[Index].Name[0] ? Table[Index].Name : "(no name)");
        gConsole.Write("\n");
    }
}

void ShellBluetoothLeConnect(int Argc, char **Argv) {
    UINTN Index = 0;

    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    if (Argc < 3 || ParseDecimal(Argv[2], &Index) != 0 || Index < 1u) {
        gConsole.Write("bt: connect <n>  (n from lescan)\n");
        return;
    }
    gConsole.Write("bt: connecting...\n");
    if (HalBluetoothLeConnect(Index, 0) != 0) {
        gConsole.Write("bt: connect fail (see bt: le …)\n");
        return;
    }
    gConsole.Write("bt: le connected\n");
}

void ShellBluetoothLeDisconnect(void) {
    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    /* BTM-10：断 LE + 拆 HOGP/Notify 会话，便于同会话 bt mouse 重连 */
    if (HalBluetoothMouseDisconnect() != 0) {
        gConsole.Write("bt: disconnect fail\n");
        return;
    }
    gConsole.Write("bt: mouse disconnected\n");
}

void ShellBluetoothSmpJustWorks(void) {
    UINT16 Stage = 0;
    UINT8 Status = 0;

    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    gConsole.Write("bt: smp pairing...\n");
    if (HalBluetoothSmpJustWorks(0) != 0) {
        HalBluetoothLeLastError(&Stage, &Status);
        gConsole.Write("bt: smp fail stage=");
        WriteHexByte((UINT8)((Stage >> 8) & 0xFFu));
        WriteHexByte((UINT8)(Stage & 0xFFu));
        gConsole.Write(" status=");
        WriteHexByte(Status);
        if (Stage == 0x0001u && Status == 0xFDu) {
            gConsole.Write(" (need bt connect first)");
        }
        gConsole.Write("\n");
        return;
    }
    gConsole.Write("bt: smp ok\n");
}

void ShellBluetoothAttHidDiscover(void) {
    UINT16 Stage = 0;
    UINT8 Status = 0;

    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    gConsole.Write("bt: hogp discover...\n");
    if (HalBluetoothAttHidDiscover(0) != 0) {
        HalBluetoothLeLastError(&Stage, &Status);
        gConsole.Write("bt: hogp fail stage=");
        WriteHexByte((UINT8)((Stage >> 8) & 0xFFu));
        WriteHexByte((UINT8)(Stage & 0xFFu));
        gConsole.Write(" status=");
        WriteHexByte(Status);
        gConsole.Write("\n");
        return;
    }
    gConsole.Write("bt: hogp ok\n");
}

void ShellBluetoothAttHidSubscribe(void) {
    UINT16 Stage = 0;
    UINT8 Status = 0;

    if (!HalBluetoothReady()) {
        gConsole.Write("bt: not ready\n");
        return;
    }
    /* BootLog 进文件/串口环，壳上看不见；提示必须走 Console */
    gConsole.Write("bt: notify subscribe — keep moving MX Master\n");
    if (HalBluetoothAttHidSubscribe(0) != 0) {
        HalBluetoothLeLastError(&Stage, &Status);
        gConsole.Write("bt: notify fail stage=");
        WriteHexByte((UINT8)((Stage >> 8) & 0xFFu));
        WriteHexByte((UINT8)(Stage & 0xFFu));
        gConsole.Write(" status=");
        WriteHexByte(Status);
        gConsole.Write("\n");
        return;
    }
    gConsole.Write("bt: notify ok (subscribed)\n");
}
