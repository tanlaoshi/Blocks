/*
 * WifiMessage.c — AIC8800 消息通道：CRC / Put16 / Get16
 *
 * TX → WifiMessageTransmit.c；RX → WifiMessageReceive.c（纯搬家）。
 */
#include "Wifi/Private.h"

/*
 * crc8，多项式 X8+X2+X+1（0x107），初值 0 —— 照抄 aicsdio.c:2004-2027。
 * PR-34 起不再 static：数据 TX 的 4 B 帧头也用同一个 CRC。
 */
UINT8 AicCrc8(const UINT8 *Buffer, UINT32 Size) {
    UINT8 Crc = 0;
    UINT32 i;
    UINT8 Bit;

    for (i = 0; i < Size; i++) {
        for (Bit = 0x80u; Bit != 0u; Bit = (UINT8)(Bit / 2u)) {
            if ((Crc & 0x80u) != 0) {
                Crc = (UINT8)(Crc * 2u);
                Crc ^= 0x07u;
            } else {
                Crc = (UINT8)(Crc * 2u);
            }
            if ((Buffer[i] & Bit) != 0u) {
                Crc ^= 0x07u;
            }
        }
    }
    return Crc;
}

void AicPut16(UINT8 *At, UINT16 Value) {
    At[0] = (UINT8)(Value & 0xFFu);
    At[1] = (UINT8)((Value >> 8) & 0xFFu);
}

UINT16 AicGet16(const UINT8 *At) {
    return (UINT16)((UINT16)At[0] | ((UINT16)At[1] << 8));
}
