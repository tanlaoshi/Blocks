/*
 * BulkOnly.c — CBW / CSW 的公共字节。
 * 与原先 xHCI、EHCI 各自填写的布局相同。
 */
#include "Msc/Private.h"

void CommandFill(UINT8 *Cbw, UINT32 Tag, const UINT8 *CbwCb, UINT8 CbLen,
                 UINT32 DataLen, int DataIn) {
    UINT32 i;

    for (i = 0; i < 32; i++) {
        Cbw[i] = 0;
    }
    Cbw[0] = 0x55;
    Cbw[1] = 0x53;
    Cbw[2] = 0x42;
    Cbw[3] = 0x43; /* USBC */
    Cbw[4] = (UINT8)(Tag);
    Cbw[5] = (UINT8)(Tag >> 8);
    Cbw[6] = (UINT8)(Tag >> 16);
    Cbw[7] = (UINT8)(Tag >> 24);
    Cbw[8] = (UINT8)(DataLen);
    Cbw[9] = (UINT8)(DataLen >> 8);
    Cbw[10] = (UINT8)(DataLen >> 16);
    Cbw[11] = (UINT8)(DataLen >> 24);
    Cbw[12] = DataIn ? 0x80u : 0x00u;
    Cbw[13] = 0; /* LUN */
    Cbw[14] = CbLen;
    for (i = 0; i < CbLen; i++) {
        Cbw[15 + i] = CbwCb[i];
    }
}

int StatusSignature(const UINT8 *Csw) {
    return Csw[0] == 0x55 && Csw[1] == 0x53 && Csw[2] == 0x42 &&
           Csw[3] == 0x53;
}
