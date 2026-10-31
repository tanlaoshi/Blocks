/*
 * WifiJoinPersist.c — WIFI-JOIN.LOG 落盘（辅助 WifiJoin.c）
 */
#include "Wifi/Private.h"
#include "FileSystem.h"
#include "Fat.h"

static void WifiJoinPersistAppendText(char *Buffer, UINTN Capacity, UINTN *Length,
                                      const char *Text) {
    UINTN Index;
    if (Text == 0) {
        return;
    }
    for (Index = 0; Text[Index] != 0 && *Length + 1u < Capacity; Index++) {
        Buffer[(*Length)++] = Text[Index];
    }
}

static void WifiJoinPersistAppendDecimal(char *Buffer, UINTN Capacity, UINTN *Length,
                                         UINT32 Value) {
    char Digits[12];
    UINT32 DigitCount = 0;
    if (Value == 0u) {
        Digits[DigitCount++] = '0';
    } else {
        while (Value != 0u && DigitCount < sizeof(Digits)) {
            Digits[DigitCount++] = (char)('0' + (Value % 10u));
            Value /= 10u;
        }
    }
    while (DigitCount > 0u && *Length + 1u < Capacity) {
        Buffer[(*Length)++] = Digits[--DigitCount];
    }
}

void WifiJoinPersistLog(const char *Status, UINT32 EventType, UINT32 EventStatus) {
    char Buffer[512];
    UINTN Length = 0;
    static const char HexDigits[] = "0123456789abcdef";
    UINT32 Tag = WIFI_BUILD_TAG;
    INT32 Nibble;

    WifiJoinPersistAppendText(Buffer, sizeof(Buffer), &Length, "tag=");
    for (Nibble = 7; Nibble >= 0; Nibble--) {
        if (Length + 1u >= sizeof(Buffer)) {
            break;
        }
        Buffer[Length++] = HexDigits[(Tag >> (UINT32)(Nibble * 4)) & 0xFu];
    }
    WifiJoinPersistAppendText(Buffer, sizeof(Buffer), &Length, "\nstatus=");
    WifiJoinPersistAppendText(Buffer, sizeof(Buffer), &Length, Status ? Status : "?");
    WifiJoinPersistAppendText(Buffer, sizeof(Buffer), &Length, "\nevent_type=");
    WifiJoinPersistAppendDecimal(Buffer, sizeof(Buffer), &Length, EventType);
    WifiJoinPersistAppendText(Buffer, sizeof(Buffer), &Length, "\nevent_status=");
    WifiJoinPersistAppendDecimal(Buffer, sizeof(Buffer), &Length, EventStatus);
    WifiJoinPersistAppendText(Buffer, sizeof(Buffer), &Length, "\n");
    Buffer[Length] = 0;
    if (FileSystemWriteFile("WIFI-JOIN.LOG", Buffer, Length) != FAT_OK) {
        HalSerialBootMarkForce("Boot: Pi WIFI join log write fail\n");
        return;
    }
    (void)FileSystemFileSync("WIFI-JOIN.LOG");
}
