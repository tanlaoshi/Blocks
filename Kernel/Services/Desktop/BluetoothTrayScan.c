/*
 * BluetoothTrayScan.c — 蓝牙托盘后台 LE Scan / Connect（不挡 Gui 光标）
 *
 * 点击只置 Want；Worker 跑 Hal。Hal 内仍分片泵 HID。
 */
#include "DesktopPrivate.h"
#include "HalBluetooth.h"

#define BT_SCAN_MAX 8u
#define BT_SCAN_TIMEOUT_MS 6000u
#define BT_CONNECT_TIMEOUT_MS 20000u

static HAL_BLUETOOTH_SCAN_ENTRY sScanTable[BT_SCAN_MAX];
static UINTN sScanCount;
static char sStatusLine[48];
static volatile int sScanWant;
static volatile int sScanBusy;
static volatile int sConnectWant;
static volatile UINTN sConnectOneBased;
static volatile int sConnectBusy;

static void StrCopy(char *Dst, int Max, const char *Src) {
    int Index = 0;

    if (Dst == 0 || Max <= 0) {
        return;
    }
    while (Src && Src[Index] && Index < Max - 1) {
        Dst[Index] = Src[Index];
        Index++;
    }
    Dst[Index] = 0;
}

static void MarkTrayRefresh(void) {
    DesktopMarkBluetoothTrayPopup();
    DesktopMarkTaskbar();
    RequestRefresh();
}

void DesktopBluetoothTrayStatusSet(const char *Text) {
    StrCopy(sStatusLine, (int)sizeof(sStatusLine), Text ? Text : "");
}

const char *DesktopBluetoothTrayStatusGet(void) {
    return sStatusLine;
}

UINTN DesktopBluetoothTrayScanCount(void) {
    return sScanCount;
}

void DesktopBluetoothTrayFormatScanRow(UINTN OneBased, char *Out, int Max) {
    const HAL_BLUETOOTH_SCAN_ENTRY *Entry;
    char Line[56];
    UINTN Index;
    int Pos = 0;

    if (Out == 0 || Max <= 0) {
        return;
    }
    if (OneBased == 0u || OneBased > sScanCount) {
        Out[0] = 0;
        return;
    }
    Entry = &sScanTable[OneBased - 1u];
    Line[Pos++] = (char)('0' + (int)(OneBased % 10u));
    Line[Pos++] = ' ';
    if (Entry->Name[0]) {
        for (Index = 0; Entry->Name[Index] && Pos < (int)sizeof(Line) - 1;
             Index++) {
            Line[Pos++] = Entry->Name[Index];
        }
    } else {
        static const char Hex[] = "0123456789ABCDEF";
        Line[Pos++] = Hex[(Entry->Address[1] >> 4) & 0xFu];
        Line[Pos++] = Hex[Entry->Address[1] & 0xFu];
        Line[Pos++] = ':';
        Line[Pos++] = Hex[(Entry->Address[0] >> 4) & 0xFu];
        Line[Pos++] = Hex[Entry->Address[0] & 0xFu];
    }
    Line[Pos] = 0;
    StrCopy(Out, Max, Line);
}

int DesktopBluetoothTrayScanIsBusy(void) {
    return (sScanBusy || sScanWant || sConnectBusy || sConnectWant) ? 1 : 0;
}

void DesktopBluetoothTrayScanRequest(void) {
    if (DesktopBluetoothTrayScanIsBusy()) {
        StrCopy(sStatusLine, (int)sizeof(sStatusLine),
                (sConnectBusy || sConnectWant) ? "connecting..."
                                               : "scanning...");
        return;
    }
    sScanWant = 1;
    StrCopy(sStatusLine, (int)sizeof(sStatusLine), "scanning...");
}

void DesktopBluetoothTrayConnectRequest(UINTN OneBased) {
    if (OneBased == 0u || OneBased > sScanCount) {
        StrCopy(sStatusLine, (int)sizeof(sStatusLine), "connect fail");
        return;
    }
    if (DesktopBluetoothTrayScanIsBusy()) {
        StrCopy(sStatusLine, (int)sizeof(sStatusLine),
                (sConnectBusy || sConnectWant) ? "connecting..."
                                               : "scanning...");
        return;
    }
    sConnectOneBased = OneBased;
    sConnectWant = 1;
    StrCopy(sStatusLine, (int)sizeof(sStatusLine), "connecting...");
}

void DesktopBluetoothTrayWorkerPoll(void) {
    int Count;
    UINTN OneBased;

    if (sScanWant && !sScanBusy && !sConnectBusy) {
        sScanWant = 0;
        sScanBusy = 1;
        Count = HalBluetoothLeScan(sScanTable, BT_SCAN_MAX, BT_SCAN_TIMEOUT_MS);
        if (Count < 0) {
            sScanCount = 0;
            StrCopy(sStatusLine, (int)sizeof(sStatusLine), "scan fail");
        } else {
            sScanCount = (UINTN)Count;
            if (sScanCount == 0u) {
                StrCopy(sStatusLine, (int)sizeof(sStatusLine), "scan: 0");
            } else {
                sStatusLine[0] = 's';
                sStatusLine[1] = 'c';
                sStatusLine[2] = 'a';
                sStatusLine[3] = 'n';
                sStatusLine[4] = ':';
                sStatusLine[5] = ' ';
                sStatusLine[6] = (char)('0' + (int)(sScanCount % 10u));
                sStatusLine[7] = 0;
            }
        }
        sScanBusy = 0;
        MarkTrayRefresh();
        return;
    }

    if (sConnectWant && !sConnectBusy && !sScanBusy) {
        OneBased = sConnectOneBased;
        sConnectWant = 0;
        sConnectBusy = 1;
        if (HalBluetoothMouseConnect(OneBased, BT_CONNECT_TIMEOUT_MS) != 0) {
            StrCopy(sStatusLine, (int)sizeof(sStatusLine), "connect fail");
        } else {
            StrCopy(sStatusLine, (int)sizeof(sStatusLine), "connected");
        }
        sConnectBusy = 0;
        MarkTrayRefresh();
    }
}
