/*
 * WifiSdpcmDhcpDiscover.c — 0x15a：SDPCM DATA 发 DHCP Discover（引 OFFER）
 * 对表 BOOTP/DHCP：eth广播 + IP 0.0.0.0→255.255.255.255 + UDP 68→67 + cookie + type=1。
 */
#include "Wifi/Private.h"

static UINT16 WifiInternetChecksum(const UINT8 *Data, UINT32 Length) {
    UINT32 Sum = 0;
    UINT32 Index;
    for (Index = 0; Index + 1u < Length; Index += 2u) {
        Sum += ((UINT32)Data[Index] << 8) | (UINT32)Data[Index + 1u];
    }
    if ((Length & 1u) != 0u) {
        Sum += (UINT32)Data[Length - 1u] << 8;
    }
    while ((Sum >> 16) != 0u) {
        Sum = (Sum & 0xffffu) + (Sum >> 16);
    }
    return (UINT16)(~Sum);
}

int WifiSdpcmSendDhcpDiscover(const UINT8 SourceMac[6]) {
    UINT8 Frame[342];
    UINT32 Index;
    UINT16 Checksum;
    UINT8 *Internet;
    UINT8 *UserDatagram;
    UINT8 *Boot;

    if (SourceMac == 0) {
        return -1;
    }
    for (Index = 0; Index < sizeof(Frame); Index++) {
        Frame[Index] = 0;
    }
    /* Ethernet */
    for (Index = 0; Index < 6u; Index++) {
        Frame[Index] = 0xFFu;
        Frame[6u + Index] = SourceMac[Index];
    }
    Frame[12] = 0x08;
    Frame[13] = 0x00;
    Internet = Frame + 14;
    Internet[0] = 0x45;
    Internet[1] = 0x00;
    Internet[2] = 0x01;
    Internet[3] = 0x48; /* total 328 = 20+8+300 */
    Internet[6] = 0x40; /* DF */
    Internet[8] = 64;
    Internet[9] = 17; /* UDP */
    for (Index = 0; Index < 4u; Index++) {
        Internet[12u + Index] = 0; /* 0.0.0.0 */
        Internet[16u + Index] = 0xFFu;
    }
    Checksum = WifiInternetChecksum(Internet, 20u);
    Internet[10] = (UINT8)((Checksum >> 8) & 0xFFu);
    Internet[11] = (UINT8)(Checksum & 0xFFu);

    UserDatagram = Internet + 20;
    UserDatagram[0] = 0;
    UserDatagram[1] = 68;
    UserDatagram[2] = 0;
    UserDatagram[3] = 67;
    UserDatagram[4] = 0x01;
    UserDatagram[5] = 0x34; /* 308 = 8+300 */
    /* UDP checksum 0 合法 */

    Boot = UserDatagram + 8;
    Boot[0] = 1; /* BOOTREQUEST */
    Boot[1] = 1; /* Ethernet */
    Boot[2] = 6;
    Boot[3] = 0;
    Boot[4] = 0x12;
    Boot[5] = 0x34;
    Boot[6] = 0x56;
    Boot[7] = 0x78; /* xid */
    for (Index = 0; Index < 6u; Index++) {
        Boot[28u + Index] = SourceMac[Index];
    }
    Boot[236] = 99;
    Boot[237] = 130;
    Boot[238] = 83;
    Boot[239] = 99; /* magic */
    Boot[240] = 53;
    Boot[241] = 1;
    Boot[242] = 1; /* DHCPDISCOVER */
    Boot[243] = 55;
    Boot[244] = 3;
    Boot[245] = 1;
    Boot[246] = 3;
    Boot[247] = 6; /* param req */
    Boot[248] = 255;

    return WifiSdpcmSendEthernet(Frame, 14u + 328u);
}
