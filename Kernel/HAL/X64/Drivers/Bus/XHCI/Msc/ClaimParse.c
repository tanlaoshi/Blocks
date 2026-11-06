/*
 * MscClaimParse.c — MSC 配置描述符解析（PR-S3-xhcimscclaim-1）
 */
#include "XHCI/Private.h"

/* 配置描述符中找 MSC Bulk IN/OUT（偏好 BOT；允许 UASP；兜底任一对 Bulk） */
int ParseBulk(UINT8 *Cfg, UINT16 Total, UINT8 *OutInterface,
                 UINT8 *EndpointIn, UINT16 *MaxPacketSizeIn, UINT8 *EndpointOut, UINT16 *MaxPacketSizeOut) {
    UINT16 Off = 0;
    UINT8 CurInterface = 0xFF;
    UINT8 CurAlt = 0;
    UINT8 InterfaceClass = 0;
    UINT8 InterfaceSub = 0;
    UINT8 InterfaceProto = 0;
    UINT8 BestInterface = 0xFF;
    UINT8 BestIn = 0;
    UINT8 BestOut = 0;
    UINT16 BestInMaxPacketSize = 0;
    UINT16 BestOutMaxPacketSize = 0;
    int BestScore = -1;

    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            CurInterface = Cfg[Off + 2];
            CurAlt = Cfg[Off + 3];
            InterfaceClass = Cfg[Off + 5];
            InterfaceSub = Cfg[Off + 6];
            InterfaceProto = Cfg[Off + 7];
        } else if (Type == 5 && Len >= 7 && CurInterface != 0xFF && CurAlt == 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            UINT16 MaxPacketSize = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
            int Score;

            if ((Attr & 0x03) != 2) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            /* class8 BOT/UASP 优先；其它 iface 仅作兜底（score 0） */
            if (InterfaceClass == 0x08) {
                Score = 10;
                if (InterfaceSub == 0x06) {
                    Score += 2;
                }
                if (InterfaceProto == 0x50 || InterfaceProto == 0x62) {
                    Score += 4;
                }
            } else {
                Score = 0;
            }
            if (Score < BestScore) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Score > BestScore) {
                BestScore = Score;
                BestInterface = CurInterface;
                BestIn = 0;
                BestOut = 0;
                BestInMaxPacketSize = 0;
                BestOutMaxPacketSize = 0;
            } else if (CurInterface != BestInterface) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Addr & 0x80) {
                BestIn = Addr;
                BestInMaxPacketSize = MaxPacketSize ? MaxPacketSize : 64;
            } else {
                BestOut = Addr;
                BestOutMaxPacketSize = MaxPacketSize ? MaxPacketSize : 64;
            }
        }
        Off = (UINT16)(Off + Len);
    }

    /* 兜底 score=0 须成对 Bulk；class8 同 */
    if (BestIn == 0 || BestOut == 0) {
        return 0;
    }
    if (BestScore < 0) {
        return 0;
    }
    /* 无 class8 时仅当找到成对 Bulk 才接受（score 0） */
    if (OutInterface) {
        *OutInterface = BestInterface;
    }
    if (EndpointIn) {
        *EndpointIn = BestIn;
    }
    if (MaxPacketSizeIn) {
        *MaxPacketSizeIn = BestInMaxPacketSize;
    }
    if (EndpointOut) {
        *EndpointOut = BestOut;
    }
    if (MaxPacketSizeOut) {
        *MaxPacketSizeOut = BestOutMaxPacketSize;
    }
    return 1;
}

void LogConfigInterfaces(UINT8 *Cfg, UINT16 Total) {
    UINT16 Off = 0;
    int N = 0;

    while (Off + 9 <= Total && N < 6) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            /* 一行汇总，避免 iface/iclass/isub/iproto 四连刷屏夹空行感 */
            BootLogHex("Boot: MSC claim iface=", Cfg[Off + 2], 2);
            N++;
        }
        Off = (UINT16)(Off + Len);
    }
}
