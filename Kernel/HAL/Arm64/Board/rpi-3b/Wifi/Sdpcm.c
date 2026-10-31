/*
 * Sdpcm.c — F2 SDPCM 控制通道：发 IOCTL / 收包（pwifi-2）
 * 对表 cyw43_ll sdpcm_send / poll_device。
 */
#include "Wifi/Private.h"

UINT8 gWifiSdpcmSequence;
UINT8 gWifiSdpcmBusCredit;
UINT16 gWifiIoctlRequestIdentity;
UINT32 gWifiIoctlLastStatus;
UINT8 gWifiSdpcmPacket[2048];

static void WifiPutLittle32(UINT8 *Destination, UINT32 Value) {
    Destination[0] = (UINT8)(Value & 0xFFu);
    Destination[1] = (UINT8)((Value >> 8) & 0xFFu);
    Destination[2] = (UINT8)((Value >> 16) & 0xFFu);
    Destination[3] = (UINT8)((Value >> 24) & 0xFFu);
}

static UINT32 WifiGetLittle32(const UINT8 *Source) {
    return (UINT32)Source[0] | ((UINT32)Source[1] << 8) | ((UINT32)Source[2] << 16) |
           ((UINT32)Source[3] << 24);
}

static UINT16 WifiGetLittle16(const UINT8 *Source) {
    return (UINT16)Source[0] | ((UINT16)Source[1] << 8);
}

static int WifiWlanWrite(const UINT8 *Buffer, UINT32 ByteCount) {
    UINT32 Remaining = ByteCount;
    const UINT8 *Cursor = Buffer;

    while (Remaining > 0) {
        UINT32 Chunk = Remaining;
        if (Chunk > WIFI_WLAN_BLOCK_BYTES) {
            Chunk = WIFI_WLAN_BLOCK_BYTES;
        }
        if (WifiSdioCommand53Write(SDIO_FUNCTION_2, 0, Cursor, Chunk, 0) != 0) {
            return -1;
        }
        Cursor += Chunk;
        Remaining -= Chunk;
    }
    return 0;
}

static int WifiWlanRead(UINT8 *Buffer, UINT32 ByteCount) {
    UINT32 Remaining = ByteCount;
    UINT8 *Cursor = Buffer;

    while (Remaining > 0) {
        UINT32 Chunk = Remaining;
        if (Chunk > WIFI_WLAN_BLOCK_BYTES) {
            Chunk = WIFI_WLAN_BLOCK_BYTES;
        }
        if (WifiSdioCommand53Read(SDIO_FUNCTION_2, 0, Cursor, Chunk, 0) != 0) {
            return -1;
        }
        Cursor += Chunk;
        Remaining -= Chunk;
    }
    return 0;
}

static int WifiSdpcmSendControl(UINT32 PayloadBytes) {
    UINT32 Size = WIFI_SDPCM_HEADER_BYTES + PayloadBytes;
    UINT8 *Header = gWifiSdpcmPacket;

    Header[0] = (UINT8)(Size & 0xFFu);
    Header[1] = (UINT8)((Size >> 8) & 0xFFu);
    Header[2] = (UINT8)((~Size) & 0xFFu);
    Header[3] = (UINT8)(((~Size) >> 8) & 0xFFu);
    Header[4] = gWifiSdpcmSequence;
    Header[5] = (UINT8)WIFI_SDPCM_CONTROL_CHANNEL;
    Header[6] = 0;
    Header[7] = (UINT8)WIFI_SDPCM_HEADER_BYTES;
    Header[8] = 0;
    Header[9] = 0;
    Header[10] = 0;
    Header[11] = 0;
    gWifiSdpcmSequence = (UINT8)(gWifiSdpcmSequence + 1u);
    return WifiWlanWrite(gWifiSdpcmPacket, Size);
}

int WifiIoctl(UINT32 Kind, UINT32 Command, UINT8 *Payload, UINT32 PayloadBytes,
              UINT32 *OutPayloadBytes) {
    UINT8 *IoctlHeader;
    UINT32 Flags;
    UINT32 Attempt;
    UINT16 RequestIdentity;

    if (WIFI_SDPCM_HEADER_BYTES + WIFI_IOCTL_HEADER_BYTES + PayloadBytes >
        sizeof(gWifiSdpcmPacket)) {
        return -1;
    }
    gWifiIoctlLastStatus = 0;
    if (gWifiSdpcmBusCredit == gWifiSdpcmSequence) {
        /* 无信用：先捅邮箱再收 */
        (void)WifiBackplaneWrite32(WIFI_SDIO_TO_SB_MAILBOX, 1u << 3);
    }

    gWifiIoctlRequestIdentity = (UINT16)(gWifiIoctlRequestIdentity + 1u);
    RequestIdentity = gWifiIoctlRequestIdentity;
    Flags = (((UINT32)RequestIdentity << WIFI_IOCTL_ID_SHIFT) & 0xFFFF0000u) | Kind;

    IoctlHeader = &gWifiSdpcmPacket[WIFI_SDPCM_HEADER_BYTES];
    WifiPutLittle32(IoctlHeader + 0, Command);
    WifiPutLittle32(IoctlHeader + 4, PayloadBytes & 0xFFFFu);
    WifiPutLittle32(IoctlHeader + 8, Flags);
    WifiPutLittle32(IoctlHeader + 12, 0);
    if (PayloadBytes != 0 && Payload != 0) {
        UINT32 Index;
        for (Index = 0; Index < PayloadBytes; Index++) {
            IoctlHeader[WIFI_IOCTL_HEADER_BYTES + Index] = Payload[Index];
        }
    }
    if (WifiSdpcmSendControl(WIFI_IOCTL_HEADER_BYTES + PayloadBytes) != 0) {
        return -1;
    }

    for (Attempt = 0; Attempt < 500u; Attempt++) {
        UINT8 HeaderBytes[4];
        UINT16 Size;
        UINT16 SizeComplement;
        UINT32 Aligned;
        UINT8 Channel;
        UINT8 HeaderLength;
        UINT16 Identity;
        UINT32 Copy;

        SdhciDelayMicroseconds(1000);
        if (WifiWlanRead(HeaderBytes, 4u) != 0) {
            continue;
        }
        Size = WifiGetLittle16(HeaderBytes);
        SizeComplement = WifiGetLittle16(HeaderBytes + 2);
        if (Size == 0 && SizeComplement == 0) {
            continue;
        }
        if ((UINT16)(Size ^ SizeComplement) != 0xFFFFu || Size < WIFI_SDPCM_HEADER_BYTES) {
            continue;
        }
        Aligned = Size - 4u;
        if (Aligned <= 8u) {
            Aligned = (Aligned + 7u) & ~7u;
        } else if (Aligned <= 16u) {
            Aligned = (Aligned + 15u) & ~15u;
        } else {
            Aligned = (Aligned + 63u) & ~63u;
        }
        if (Aligned + 4u > sizeof(gWifiSdpcmPacket)) {
            continue;
        }
        if (WifiWlanRead(gWifiSdpcmPacket + 4, Aligned) != 0) {
            continue;
        }
        gWifiSdpcmPacket[0] = HeaderBytes[0];
        gWifiSdpcmPacket[1] = HeaderBytes[1];
        gWifiSdpcmPacket[2] = HeaderBytes[2];
        gWifiSdpcmPacket[3] = HeaderBytes[3];

        Channel = (UINT8)(gWifiSdpcmPacket[5] & 0x0Fu);
        HeaderLength = gWifiSdpcmPacket[7];
        gWifiSdpcmBusCredit = gWifiSdpcmPacket[9];
        if (Channel != WIFI_SDPCM_CONTROL_CHANNEL) {
            /* 事件/数据：扫描路径另收；此处跳过 */
            continue;
        }
        if (HeaderLength + WIFI_IOCTL_HEADER_BYTES > Size) {
            continue;
        }
        Identity = (UINT16)((WifiGetLittle32(gWifiSdpcmPacket + HeaderLength + 8) >>
                             WIFI_IOCTL_ID_SHIFT) &
                            0xFFFFu);
        if (Identity != RequestIdentity) {
            continue;
        }
        /* 0x145：CDC status@+12；非 0 则固件拒了 IOCTL（旧码当成功 → 假通） */
        gWifiIoctlLastStatus = WifiGetLittle32(gWifiSdpcmPacket + HeaderLength + 12);
        if (gWifiIoctlLastStatus != 0u) {
            return -1;
        }
        Copy = Size - (UINT32)HeaderLength - WIFI_IOCTL_HEADER_BYTES;
        if (Payload != 0 && Copy > 0) {
            UINT32 Index;
            if (Copy > PayloadBytes) {
                Copy = PayloadBytes;
            }
            for (Index = 0; Index < Copy; Index++) {
                Payload[Index] =
                    gWifiSdpcmPacket[HeaderLength + WIFI_IOCTL_HEADER_BYTES + Index];
            }
        }
        if (OutPayloadBytes != 0) {
            *OutPayloadBytes = Copy;
        }
        return 0;
    }
    return -1;
}

int WifiIoctlSetVariable(const char *Name, const UINT8 *Value, UINT32 ValueBytes) {
    UINT8 Buffer[256];
    UINT32 NameBytes = 0;
    UINT32 Index;

    while (Name[NameBytes] != 0) {
        NameBytes++;
    }
    if (NameBytes + 1u + ValueBytes > sizeof(Buffer)) {
        return -1;
    }
    for (Index = 0; Index < NameBytes; Index++) {
        Buffer[Index] = (UINT8)Name[Index];
    }
    Buffer[NameBytes] = 0;
    for (Index = 0; Index < ValueBytes; Index++) {
        Buffer[NameBytes + 1u + Index] = Value[Index];
    }
    return WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_SET_VAR, Buffer, NameBytes + 1u + ValueBytes, 0);
}
