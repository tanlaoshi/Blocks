/*
 * WifiNetProbeLog.c — WIFI-NET.LOG 落盘（辅助 WifiNetProbe）
 */
#include "Wifi/Private.h"
#include "FileSystem.h"
#include "Fat.h"

static void WifiNetLogAppendText(char *Buffer, UINTN Capacity, UINTN *Length, const char *Text) {
    UINTN Index;
    if (Text == 0) {
        return;
    }
    for (Index = 0; Text[Index] != 0 && *Length + 1u < Capacity; Index++) {
        Buffer[(*Length)++] = Text[Index];
    }
}

static void WifiNetLogAppendDecimal(char *Buffer, UINTN Capacity, UINTN *Length, UINT32 Value) {
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

static void WifiNetLogAppendField(char *Buffer, UINTN Capacity, UINTN *Length, const char *Name,
                                  UINT32 Value) {
    WifiNetLogAppendText(Buffer, Capacity, Length, Name);
    WifiNetLogAppendDecimal(Buffer, Capacity, Length, Value);
    WifiNetLogAppendText(Buffer, Capacity, Length, "\n");
}

void WifiNetProbeLogWrite(const char *Status, const UINT8 *Mac, int MacOk, UINT32 MacOutBytes,
                          UINT32 DataPackets, UINT32 EventPackets, UINT32 OtherPackets,
                          UINT32 PollEmpty, UINT32 PollError, UINT32 FirstEtherType,
                          UINT32 ArpTarget, UINT32 TransmitOk, UINT32 ArpReply,
                          UINT32 LwIpInput, UINT32 LwIpOutput, UINT32 ArpOffloadCleared,
                          UINT32 DhcpIpHostOrder) {
    char Buffer[832];
    UINTN Length = 0;
    static const char HexDigits[] = "0123456789abcdef";
    UINT32 Tag = WIFI_BUILD_TAG;
    INT32 Nibble;
    UINT32 Index;

    WifiNetLogAppendText(Buffer, sizeof(Buffer), &Length, "tag=");
    for (Nibble = 7; Nibble >= 0; Nibble--) {
        if (Length + 1u >= sizeof(Buffer)) {
            break;
        }
        Buffer[Length++] = HexDigits[(Tag >> (UINT32)(Nibble * 4)) & 0xFu];
    }
    WifiNetLogAppendText(Buffer, sizeof(Buffer), &Length, "\nstatus=");
    WifiNetLogAppendText(Buffer, sizeof(Buffer), &Length, Status ? Status : "?");
    WifiNetLogAppendText(Buffer, sizeof(Buffer), &Length, "\nmac=");
    if (MacOk && Mac != 0) {
        for (Index = 0; Index < 6u; Index++) {
            if (Index != 0u) {
                WifiNetLogAppendText(Buffer, sizeof(Buffer), &Length, ":");
            }
            if (Length + 2u < sizeof(Buffer)) {
                Buffer[Length++] = HexDigits[(Mac[Index] >> 4) & 0xFu];
                Buffer[Length++] = HexDigits[Mac[Index] & 0xFu];
            }
        }
    } else {
        WifiNetLogAppendText(Buffer, sizeof(Buffer), &Length, "?");
    }
    WifiNetLogAppendText(Buffer, sizeof(Buffer), &Length, "\n");
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "mac_out=", MacOutBytes);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "data_pkts=", DataPackets);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "event_pkts=", EventPackets);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "other_pkts=", OtherPackets);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "poll_empty=", PollEmpty);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "poll_error=", PollError);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "first_etype=", FirstEtherType);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "arp_target=", ArpTarget);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "transmit_ok=", TransmitOk);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "arp_reply=", ArpReply);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "lwip_in=", LwIpInput);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "lwip_out=", LwIpOutput);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "arp_ol_off=", ArpOffloadCleared);
    WifiNetLogAppendField(Buffer, sizeof(Buffer), &Length, "dhcp_ip=", DhcpIpHostOrder);
    Buffer[Length] = 0;
    if (FileSystemWriteFile("WIFI-NET.LOG", Buffer, Length) != FAT_OK) {
        HalSerialBootMarkForce("Boot: Pi WIFI net log write fail\n");
        return;
    }
    (void)FileSystemFileSync("WIFI-NET.LOG");
}
