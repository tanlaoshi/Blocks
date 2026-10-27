/*
 * Info.c — BD_ADDR + 本地名 + Ready（btx-3）
 *
 * Reset 成功后：Read_BD_ADDR；Write_Local_Name("Blocks")；Ready=1。
 */
#include "XHCI/Bluetooth/Private.h"

int gBluetoothReady;
UINT8 gBluetoothAddress[6];
char gBluetoothLocalName[64];

static void LogAddressLine(void) {
    static const char Digits[] = "0123456789ABCDEF";
    char Line[72];
    int N = 0;
    int B;
    const char *P = "Boot: Bluetooth addr=";

    while (*P && N < 60) {
        Line[N++] = *P++;
    }
    /* 展示序：HCI 线序 LSB 在前 → 人读高字节在前；无 0x */
    for (B = 5; B >= 0; B--) {
        UINT8 V = gBluetoothAddress[B];

        if (N + 2 < 70) {
            Line[N++] = Digits[(V >> 4) & 0x0Fu];
            Line[N++] = Digits[V & 0x0Fu];
        }
        if (B > 0 && N < 70) {
            Line[N++] = ':';
        }
    }
    P = " name=";
    while (*P && N < 60) {
        Line[N++] = *P++;
    }
    P = gBluetoothLocalName;
    while (*P && N < 70) {
        Line[N++] = *P++;
    }
    if (N < 71) {
        Line[N++] = '\n';
    }
    Line[N] = 0;
    BootLog(Line);
}

static int ReadBdAddress(void) {
    UINT8 Status = 0xFF;
    UINT8 Addr[6];
    UINT8 RetLen = 0;
    UINT32 I;

    if (BluetoothHciCommand(HCI_OPCODE_READ_BD_ADDR, 0, 0, &Status, Addr,
                            6, &RetLen) < 0) {
        BootLog("Boot: Bluetooth Read_BD_ADDR fail\n");
        return 0;
    }
    if (Status != 0 || RetLen < 6u) {
        BootLogHex("Boot: Bluetooth Read_BD_ADDR status=", Status, 2);
        return 0;
    }
    for (I = 0; I < 6u; I++) {
        gBluetoothAddress[I] = Addr[I];
    }
    return 1;
}

static int WriteLocalNameBlocks(void) {
    UINT8 Name[BLUETOOTH_NAME_MAX];
    UINT8 Status = 0xFF;
    const char *Src = "Blocks";
    UINT32 I;

    ZeroMemory(Name, sizeof(Name));
    for (I = 0; Src[I] && I < sizeof(Name); I++) {
        Name[I] = (UINT8)Src[I];
    }
    if (BluetoothHciCommand(HCI_OPCODE_WRITE_LOCAL_NAME, Name,
                            BLUETOOTH_NAME_MAX, &Status, 0, 0, 0) < 0) {
        BootLog("Boot: Bluetooth Write_Local_Name fail\n");
        return 0;
    }
    if (Status != 0) {
        BootLogHex("Boot: Bluetooth Write_Local_Name status=", Status, 2);
        return 0;
    }
    for (I = 0; I + 1u < sizeof(gBluetoothLocalName) && Src[I]; I++) {
        gBluetoothLocalName[I] = Src[I];
    }
    gBluetoothLocalName[I] = 0;
    return 1;
}

int BluetoothBringUpIdentity(void) {
    gBluetoothReady = 0;
    gBluetoothLocalName[0] = 0;
    ZeroMemory(gBluetoothAddress, sizeof(gBluetoothAddress));
    if (!gBluetoothHciResetOk) {
        return 0;
    }
    /* Reset 后控制器再就绪；过短连发下一条易丢事件（Intel USB HCI） */
    if (!HalCpuIsHypervisor()) {
        StallMs(50);
    }
    if (!ReadBdAddress()) {
        return 0;
    }
    if (!WriteLocalNameBlocks()) {
        /* 地址已有：仍可演示 Ready；名失败只打日志 */
        gBluetoothLocalName[0] = 'B';
        gBluetoothLocalName[1] = 'l';
        gBluetoothLocalName[2] = 'o';
        gBluetoothLocalName[3] = 'c';
        gBluetoothLocalName[4] = 'k';
        gBluetoothLocalName[5] = 's';
        gBluetoothLocalName[6] = 0;
        BootLog("Boot: Bluetooth name fallback Blocks (write fail)\n");
    }
    gBluetoothReady = 1;
    LogAddressLine();
    BootLog("Boot: Bluetooth Ready\n");
    return 1;
}
