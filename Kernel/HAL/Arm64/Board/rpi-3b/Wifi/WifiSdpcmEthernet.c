/*
 * WifiSdpcmEthernet.c — SDPCM DATA 通道发以太网帧（对表 cyw43_ll_send_ethernet）
 * 布局：SDPCM(12) + pad(2) + BDC(4) + eth；header_length=14；channel=DATA。
 */
#include "Wifi/Private.h"

#define WIFI_SDPCM_DATA_HEADER_LENGTH (WIFI_SDPCM_HEADER_BYTES + 2u)
#define WIFI_SDPCM_BDC_BYTES          4u
#define WIFI_SDPCM_DATA_PREFIX_BYTES  (2u + WIFI_SDPCM_BDC_BYTES) /* pad + BDC */
#define WIFI_SDPCM_CREDIT_WAIT_ATTEMPTS 1000u

static int WifiSdpcmWlanWrite(const UINT8 *Buffer, UINT32 ByteCount) {
    UINT32 Remaining = ByteCount;
    const UINT8 *Cursor = Buffer;

    while (Remaining > 0u) {
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

static int WifiSdpcmWaitForBusCredit(void) {
    UINT32 Attempt;

    if (gWifiSdpcmBusCredit != gWifiSdpcmSequence) {
        return 0;
    }
    (void)WifiBackplaneWrite32(WIFI_SDIO_TO_SB_MAILBOX, 1u << 3);
    for (Attempt = 0; Attempt < WIFI_SDPCM_CREDIT_WAIT_ATTEMPTS; Attempt++) {
        UINT8 Channel = 0;
        UINT32 Offset = 0;
        UINT32 Length = 0;
        int Poll;

        if ((Attempt % 100u) == 0u) {
            (void)WifiBackplaneWrite32(WIFI_SDIO_TO_SB_MAILBOX, 1u << 3);
            (void)WifiSdioSleepControlEnsureAwake();
        }
        Poll = WifiPollOnePacket(&Channel, &Offset, &Length);
        (void)Offset;
        (void)Length;
        (void)Channel;
        if (gWifiSdpcmBusCredit != gWifiSdpcmSequence) {
            return 0;
        }
        if (Poll != 0) {
            SdhciDelayMicroseconds(1000);
        }
    }
    return -1;
}

int WifiSdpcmSendEthernet(const UINT8 *Frame, UINT32 FrameBytes) {
    UINT32 Size;
    UINT8 *Header;
    UINT8 *Bdc;
    UINT32 Index;

    if (Frame == 0 || FrameBytes < 14u) {
        return -1;
    }
    if (WIFI_SDPCM_HEADER_BYTES + WIFI_SDPCM_DATA_PREFIX_BYTES + FrameBytes >
        sizeof(gWifiSdpcmPacket)) {
        return -1;
    }
    if (WifiSdpcmWaitForBusCredit() != 0) {
        return -1;
    }

    Size = WIFI_SDPCM_HEADER_BYTES + WIFI_SDPCM_DATA_PREFIX_BYTES + FrameBytes;
    Header = gWifiSdpcmPacket;
    Header[0] = (UINT8)(Size & 0xFFu);
    Header[1] = (UINT8)((Size >> 8) & 0xFFu);
    Header[2] = (UINT8)((~Size) & 0xFFu);
    Header[3] = (UINT8)(((~Size) >> 8) & 0xFFu);
    Header[4] = gWifiSdpcmSequence;
    Header[5] = (UINT8)WIFI_SDPCM_DATA_CHANNEL;
    Header[6] = 0;
    Header[7] = (UINT8)WIFI_SDPCM_DATA_HEADER_LENGTH;
    Header[8] = 0;
    Header[9] = 0;
    Header[10] = 0;
    Header[11] = 0;
    /* pad @12..13；BDC @14；对表 cyw43 flags=0x20 */
    Header[12] = 0;
    Header[13] = 0;
    Bdc = Header + WIFI_SDPCM_DATA_HEADER_LENGTH;
    Bdc[0] = 0x20;
    Bdc[1] = 0;
    Bdc[2] = 0; /* STA 接口 */
    Bdc[3] = 0; /* data_offset 以 4 字节计 */
    for (Index = 0; Index < FrameBytes; Index++) {
        Header[WIFI_SDPCM_HEADER_BYTES + WIFI_SDPCM_DATA_PREFIX_BYTES + Index] = Frame[Index];
    }
    gWifiSdpcmSequence = (UINT8)(gWifiSdpcmSequence + 1u);
    return WifiSdpcmWlanWrite(gWifiSdpcmPacket, Size);
}

int WifiSdpcmSendArpWhoHas(const UINT8 SourceMac[6], UINT32 TargetProtocolAddress) {
    UINT8 Frame[42];
    UINT32 Index;

    if (SourceMac == 0) {
        return -1;
    }
    for (Index = 0; Index < 6u; Index++) {
        Frame[Index] = 0xFFu; /* 广播 */
        Frame[6u + Index] = SourceMac[Index];
    }
    Frame[12] = 0x08;
    Frame[13] = 0x06;
    Frame[14] = 0x00;
    Frame[15] = 0x01; /* Ethernet */
    Frame[16] = 0x08;
    Frame[17] = 0x00; /* IPv4 */
    Frame[18] = 6;
    Frame[19] = 4;
    Frame[20] = 0x00;
    Frame[21] = 0x01; /* request */
    for (Index = 0; Index < 6u; Index++) {
        Frame[22u + Index] = SourceMac[Index];
    }
    Frame[28] = 0;
    Frame[29] = 0;
    Frame[30] = 0;
    Frame[31] = 0; /* SPA 未知（未 DHCP） */
    for (Index = 0; Index < 6u; Index++) {
        Frame[32u + Index] = 0;
    }
    Frame[38] = (UINT8)((TargetProtocolAddress >> 24) & 0xFFu);
    Frame[39] = (UINT8)((TargetProtocolAddress >> 16) & 0xFFu);
    Frame[40] = (UINT8)((TargetProtocolAddress >> 8) & 0xFFu);
    Frame[41] = (UINT8)(TargetProtocolAddress & 0xFFu);
    return WifiSdpcmSendEthernet(Frame, sizeof(Frame));
}
